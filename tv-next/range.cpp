#include "tv-next/range.h"

#include "llvm/IR/ConstantRange.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/Instructions.h"

#include <algorithm>

namespace alive_tv_next {

namespace {

// Derives complementary signed or unsigned bounds when the sign is known.
void crossDerive(KnownRange &r) {
  if (r.u && !r.s && !r.u->second.isNegative())
    r.s = r.u;
  if (r.s && !r.u && !r.s->first.isNegative())
    r.u = r.s;
}

// Retrieves KnownBits for V from map, evaluates constant values directly, or
// returns an unconstrained bitmask at the value bitwidth.
llvm::KnownBits knownBitsOf(const llvm::Value *V, const RangeMap &map) {
  if (auto *CI = llvm::dyn_cast<llvm::ConstantInt>(V))
    return llvm::KnownBits::makeConstant(CI->getValue());
  auto it = map.find(V);
  if (it != map.end() && it->second.bits)
    return *it->second.bits;
  unsigned bw =
      V->getType()->isIntegerTy() ? V->getType()->getIntegerBitWidth() : 0;
  return llvm::KnownBits(bw);
}

// Tightens r.bits using interval bounds via KnownBits::makeConstantRange.
void deriveBitsFromIntervals(KnownRange &r, unsigned bw) {
  if (bw == 0)
    return;
  llvm::KnownBits acc(bw);
  if (r.bits)
    acc = *r.bits;
  auto add = [&](llvm::KnownBits kb) {
    acc.Zero |= kb.Zero;
    acc.One |= kb.One;
  };
  if (r.u && r.u->first.ule(r.u->second)) {
    llvm::ConstantRange cr(r.u->first, r.u->second + 1);
    add(cr.toKnownBits());
  }
  if (r.s && r.s->first.sle(r.s->second)) {
    // Approximate signed bits using an unsigned interval. toKnownBits returns
    // common bits shared across the interval.
    llvm::APInt lo = r.s->first, hi = r.s->second + 1;
    if (lo.ule(hi)) {
      llvm::ConstantRange cr(lo, hi);
      add(cr.toKnownBits());
    }
  }
  if (!acc.isUnknown())
    r.bits = acc;
}

// Resolves range for V, synthesizing a well-defined range for ConstantInt
// values. Returns nullptr when V has no known range information.
const KnownRange *rangeOf(const llvm::Value *V, const RangeMap &map,
                          KnownRange &storage) {
  auto it = map.find(V);
  if (it != map.end())
    return &it->second;
  if (auto *CI = llvm::dyn_cast<llvm::ConstantInt>(V)) {
    storage = KnownRange{};
    storage.u = {CI->getValue(), CI->getValue()};
    storage.s = {CI->getValue(), CI->getValue()};
    storage.bits = llvm::KnownBits::makeConstant(CI->getValue());
    storage.undef_free = true;
    storage.poison_free = true;
    return &storage;
  }
  return nullptr;
}

// Tests whether a shift amount operand is strictly less than the bitwidth.
// Returns {in_range, const_shift} where const_shift is bw when non-constant.
std::pair<bool, unsigned> shiftInRange(llvm::Value *amt_op, unsigned bw,
                                       const KnownRange *rR) {
  if (auto *C = llvm::dyn_cast<llvm::ConstantInt>(amt_op)) {
    unsigned sh = (unsigned)C->getValue().getLimitedValue(bw);
    return {sh < bw, sh};
  }
  if (rR && rR->u && rR->u->second.ult(bw))
    return {true, bw}; // in range, but not a constant
  return {false, bw};
}

std::optional<KnownRange> transfer(const llvm::Instruction *I,
                                   const RangeMap &map) {
  if (!I->getType()->isIntegerTy())
    return std::nullopt;
  unsigned bw = I->getType()->getIntegerBitWidth();

  KnownRange lhsS, rhsS, srcS, condS;

  // select
  if (auto *sel = llvm::dyn_cast<llvm::SelectInst>(I)) {
    const KnownRange *condR = rangeOf(sel->getCondition(), map, condS);
    const KnownRange *trueR = rangeOf(sel->getTrueValue(), map, lhsS);
    const KnownRange *falseR = rangeOf(sel->getFalseValue(), map, rhsS);
    if (!trueR && !falseR)
      return std::nullopt;

    KnownRange r;
    if (trueR && trueR->u && falseR && falseR->u) {
      auto lo = trueR->u->first.ult(falseR->u->first) ? trueR->u->first
                                                      : falseR->u->first;
      auto hi = trueR->u->second.ugt(falseR->u->second) ? trueR->u->second
                                                        : falseR->u->second;
      r.u = {lo, hi};
    }
    if (trueR && trueR->s && falseR && falseR->s) {
      auto lo = trueR->s->first.slt(falseR->s->first) ? trueR->s->first
                                                      : falseR->s->first;
      auto hi = trueR->s->second.sgt(falseR->s->second) ? trueR->s->second
                                                        : falseR->s->second;
      r.s = {lo, hi};
    }
    // A bit is known only when both branches agree. intersectWith computes
    // the bitwise intersection of known bits across branches.
    {
      llvm::KnownBits tb = knownBitsOf(sel->getTrueValue(), map);
      llvm::KnownBits fb = knownBitsOf(sel->getFalseValue(), map);
      r.bits = tb.intersectWith(fb);
    }
    if (!r.u && !r.s && (!r.bits || r.bits->isUnknown()))
      return std::nullopt;
    // All three operands must be well-defined for the result to be.
    r.undef_free = (condR && condR->undef_free) &&
                   (trueR && trueR->undef_free) &&
                   (falseR && falseR->undef_free);
    r.poison_free = (condR && condR->poison_free) &&
                    (trueR && trueR->poison_free) &&
                    (falseR && falseR->poison_free);
    crossDerive(r);
    deriveBitsFromIntervals(r, bw);
    return r;
  }

  // freeze
  if (auto *fz = llvm::dyn_cast<llvm::FreezeInst>(I)) {
    const KnownRange *srcR = rangeOf(fz->getOperand(0), map, srcS);
    KnownRange r;
    if (srcR) {
      r.u = srcR->u;
      r.s = srcR->s;
      r.bits = srcR->bits;
    }
    // freeze always produces a well-defined value, regardless of input.
    r.undef_free = true;
    r.poison_free = true;
    deriveBitsFromIntervals(r, bw);
    return r;
  }

  // binary operators
  if (auto *BO = llvm::dyn_cast<llvm::BinaryOperator>(I)) {
    const KnownRange *lR = rangeOf(BO->getOperand(0), map, lhsS);
    const KnownRange *rR = rangeOf(BO->getOperand(1), map, rhsS);

    switch (BO->getOpcode()) {

    case llvm::Instruction::And: {
      KnownRange r;
      // and X, C for non-negative C yields [0, C]. Applies to any operand with
      // a non-negative upper bound.
      auto tryConst = [&](const KnownRange *xR, const KnownRange *cR) {
        if (cR && cR->u && !cR->u->second.isNegative())
          r.u = {llvm::APInt::getZero(bw), cR->u->second};
      };
      tryConst(lR, rR);
      if (!r.u)
        tryConst(rR, lR);
      // and X, Y (both u known): [0, min(hi_x, hi_y)].
      if (!r.u && lR && lR->u && rR && rR->u) {
        auto hi =
            lR->u->second.ult(rR->u->second) ? lR->u->second : rR->u->second;
        r.u = {llvm::APInt::getZero(bw), hi};
      }
      // KnownBits: and propagates zeros from either side (and ones from both).
      r.bits = knownBitsOf(BO->getOperand(0), map) &
               knownBitsOf(BO->getOperand(1), map);
      if (!r.u && r.bits->isUnknown())
        return std::nullopt;
      r.undef_free = (lR && lR->undef_free) && (rR && rR->undef_free);
      r.poison_free = (lR && lR->poison_free) && (rR && rR->poison_free);
      crossDerive(r);
      deriveBitsFromIntervals(r, bw);
      return r;
    }

    case llvm::Instruction::Or: {
      KnownRange r;
      if (lR && lR->u && rR && rR->u) {
        auto lo = lR->u->first.ugt(rR->u->first) ? lR->u->first : rR->u->first;
        unsigned kx = lR->u->second.getActiveBits();
        unsigned ky = rR->u->second.getActiveBits();
        unsigned maxBits = std::max(kx, ky);
        auto hi = maxBits == 0 ? llvm::APInt::getZero(bw)
                               : llvm::APInt::getLowBitsSet(bw, maxBits);
        r.u = {lo, hi};
      }
      // KnownBits: or propagates ones from either side (and zeros from both).
      r.bits = knownBitsOf(BO->getOperand(0), map) |
               knownBitsOf(BO->getOperand(1), map);
      if (!r.u && r.bits->isUnknown())
        return std::nullopt;
      r.undef_free = (lR && lR->undef_free) && (rR && rR->undef_free);
      r.poison_free = (lR && lR->poison_free) && (rR && rR->poison_free);
      crossDerive(r);
      deriveBitsFromIntervals(r, bw);
      return r;
    }

    case llvm::Instruction::Xor: {
      KnownRange r;
      r.bits = knownBitsOf(BO->getOperand(0), map) ^
               knownBitsOf(BO->getOperand(1), map);
      if (r.bits->isUnknown())
        return std::nullopt;
      r.undef_free = (lR && lR->undef_free) && (rR && rR->undef_free);
      r.poison_free = (lR && lR->poison_free) && (rR && rR->poison_free);
      deriveBitsFromIntervals(r, bw);
      return r;
    }

    case llvm::Instruction::URem: {
      // Non-zero divisor lower bound avoids division-by-zero poison.
      if (!rR || !rR->u || rR->u->first.isZero())
        return std::nullopt;
      KnownRange r;
      // result in [0, hi_d - 1] because urem(x, d) < d for any d > 0.
      r.u = {llvm::APInt::getZero(bw), rR->u->second - 1};
      r.bits = llvm::KnownBits::urem(knownBitsOf(BO->getOperand(0), map),
                                     knownBitsOf(BO->getOperand(1), map));
      r.undef_free = (lR && lR->undef_free) && rR->undef_free;
      r.poison_free = (lR && lR->poison_free) && rR->poison_free;
      crossDerive(r);
      deriveBitsFromIntervals(r, bw);
      return r;
    }

    case llvm::Instruction::UDiv: {
      // Requires non-zero divisor lower bound and known dividend bounds.
      if (!rR || !rR->u || rR->u->first.isZero() || !lR || !lR->u)
        return std::nullopt;
      // result in [lo_x / hi_d, hi_x / lo_d].
      KnownRange r;
      r.u = {lR->u->first.udiv(rR->u->second),
             lR->u->second.udiv(rR->u->first)};
      r.bits = llvm::KnownBits::udiv(knownBitsOf(BO->getOperand(0), map),
                                     knownBitsOf(BO->getOperand(1), map));
      r.undef_free = lR->undef_free && rR->undef_free;
      r.poison_free = lR->poison_free && rR->poison_free;
      crossDerive(r);
      deriveBitsFromIntervals(r, bw);
      return r;
    }

    case llvm::Instruction::LShr: {
      // Shift amount must be provably less than bitwidth.
      auto [in_range, const_shift] = shiftInRange(BO->getOperand(1), bw, rR);
      if (!in_range)
        return std::nullopt;

      KnownRange r;
      if (lR && lR->u) {
        if (const_shift < bw) {
          // Constant shift: tight bounds.
          r.u = {lR->u->first.lshr(const_shift),
                 lR->u->second.lshr(const_shift)};
        } else if (rR && rR->u) {
          // Variable shift in [lo_amt, hi_amt]:
          //   min result = lo_x >> hi_amt (smallest x shifted the most)
          //   max result = hi_x >> lo_amt (largest x shifted the least)
          unsigned hi_amt = (unsigned)rR->u->second.getLimitedValue();
          unsigned lo_amt = (unsigned)rR->u->first.getLimitedValue();
          r.u = {lR->u->first.lshr(hi_amt), lR->u->second.lshr(lo_amt)};
        }
      }
      r.bits = llvm::KnownBits::lshr(knownBitsOf(BO->getOperand(0), map),
                                     knownBitsOf(BO->getOperand(1), map));
      // Shift is in range: no shift-amount-induced poison; propagate flags.
      r.undef_free = (lR && lR->undef_free) && (rR && rR->undef_free);
      r.poison_free = (lR && lR->poison_free) && (rR && rR->poison_free);
      if (!r.u && !r.s && !r.well_defined() && (!r.bits || r.bits->isUnknown()))
        return std::nullopt;
      crossDerive(r);
      deriveBitsFromIntervals(r, bw);
      return r;
    }

    case llvm::Instruction::AShr: {
      // Shift amount must be provably less than bitwidth.
      auto [in_range, const_shift] = shiftInRange(BO->getOperand(1), bw, rR);
      if (!in_range)
        return std::nullopt;

      KnownRange r;
      if (lR && lR->s) {
        if (const_shift < bw) {
          // Constant shift: tight bounds.
          r.s = {lR->s->first.ashr(const_shift),
                 lR->s->second.ashr(const_shift)};
        } else if (rR && rR->u) {
          // Variable shift in [lo_amt, hi_amt]:
          //   tightest signed bound uses lo_amt (least shift):
          //   min result = lo_x >> lo_amt, max result = hi_x >> lo_amt.
          unsigned lo_amt = (unsigned)rR->u->first.getLimitedValue();
          r.s = {lR->s->first.ashr(lo_amt), lR->s->second.ashr(lo_amt)};
        }
      }
      r.bits = llvm::KnownBits::ashr(knownBitsOf(BO->getOperand(0), map),
                                     knownBitsOf(BO->getOperand(1), map));
      // Shift is in range: no shift-amount-induced poison; propagate flags.
      r.undef_free = (lR && lR->undef_free) && (rR && rR->undef_free);
      r.poison_free = (lR && lR->poison_free) && (rR && rR->poison_free);
      if (!r.u && !r.s && !r.well_defined() && (!r.bits || r.bits->isUnknown()))
        return std::nullopt;
      crossDerive(r);
      deriveBitsFromIntervals(r, bw);
      return r;
    }

    case llvm::Instruction::Shl: {
      // Shift amount must be provably less than bitwidth.
      auto [in_range, const_shift] = shiftInRange(BO->getOperand(1), bw, rR);
      if (!in_range)
        return std::nullopt;

      bool nuw = BO->hasNoUnsignedWrap();
      bool nsw = BO->hasNoSignedWrap();
      KnownRange r;
      // Tight value bounds are only derivable for constant shift amounts.
      if (const_shift < bw) {
        if (nuw && lR && lR->u)
          r.u = {lR->u->first << const_shift, lR->u->second << const_shift};
        if (nsw && lR && lR->s)
          r.s = {lR->s->first << const_shift, lR->s->second << const_shift};
      }
      r.bits = llvm::KnownBits::shl(knownBitsOf(BO->getOperand(0), map),
                                    knownBitsOf(BO->getOperand(1), map),
                                    /*NUW=*/nuw, /*NSW=*/nsw);
      // Shift is in range: no shift-amount-induced poison; propagate flags.
      r.undef_free = (lR && lR->undef_free) && (rR && rR->undef_free);
      r.poison_free = (lR && lR->poison_free) && (rR && rR->poison_free);
      if (!r.u && !r.s && !r.well_defined() && (!r.bits || r.bits->isUnknown()))
        return std::nullopt;
      crossDerive(r);
      deriveBitsFromIntervals(r, bw);
      return r;
    }

    case llvm::Instruction::Add: {
      bool nuw = BO->hasNoUnsignedWrap();
      bool nsw = BO->hasNoSignedWrap();
      KnownRange r;
      if (nuw && lR && lR->u && rR && rR->u)
        r.u = {lR->u->first + rR->u->first, lR->u->second + rR->u->second};
      if (nsw && lR && lR->s && rR && rR->s)
        r.s = {lR->s->first + rR->s->first, lR->s->second + rR->s->second};
      r.bits = llvm::KnownBits::computeForAddSub(
          /*Add=*/true, nsw, nuw, knownBitsOf(BO->getOperand(0), map),
          knownBitsOf(BO->getOperand(1), map));
      r.undef_free = (lR && lR->undef_free) && (rR && rR->undef_free);
      // Wrapping addition cannot produce poison. For nuw and nsw addition,
      // poison_free is established only when bounds confirm absence of wrap.
      if (!(nuw || nsw) || r.u || r.s)
        r.poison_free = (lR && lR->poison_free) && (rR && rR->poison_free);
      if (!r.u && !r.s && !r.well_defined() && (!r.bits || r.bits->isUnknown()))
        return std::nullopt;
      crossDerive(r);
      deriveBitsFromIntervals(r, bw);
      return r;
    }

    case llvm::Instruction::Sub: {
      bool nuw = BO->hasNoUnsignedWrap();
      bool nsw = BO->hasNoSignedWrap();
      KnownRange r;
      if (nuw && lR && lR->u && rR && rR->u && lR->u->second.uge(rR->u->first))
        r.u = {llvm::APInt::getZero(bw), lR->u->second - rR->u->first};
      if (nsw && lR && lR->s && rR && rR->s && !lR->s->first.isNegative() &&
          !rR->s->second.isNegative() && lR->s->second.sge(rR->s->first))
        r.s = {llvm::APInt::getZero(bw), lR->s->second - rR->s->first};
      r.bits = llvm::KnownBits::computeForAddSub(
          /*Add=*/false, nsw, nuw, knownBitsOf(BO->getOperand(0), map),
          knownBitsOf(BO->getOperand(1), map));
      r.undef_free = (lR && lR->undef_free) && (rR && rR->undef_free);
      // Wrapping subtraction cannot produce poison.
      if (!(nuw || nsw) || r.u || r.s)
        r.poison_free = (lR && lR->poison_free) && (rR && rR->poison_free);
      if (!r.u && !r.s && !r.well_defined() && (!r.bits || r.bits->isUnknown()))
        return std::nullopt;
      crossDerive(r);
      deriveBitsFromIntervals(r, bw);
      return r;
    }

    case llvm::Instruction::Mul: {
      bool nuw = BO->hasNoUnsignedWrap();
      bool nsw = BO->hasNoSignedWrap();
      KnownRange r;
      if (nuw && lR && lR->u && rR && rR->u)
        r.u = {lR->u->first * rR->u->first, lR->u->second * rR->u->second};
      if (nsw && lR && lR->s && rR && rR->s && !lR->s->first.isNegative() &&
          !rR->s->first.isNegative())
        r.s = {lR->s->first * rR->s->first, lR->s->second * rR->s->second};
      r.bits = llvm::KnownBits::mul(knownBitsOf(BO->getOperand(0), map),
                                    knownBitsOf(BO->getOperand(1), map));
      r.undef_free = (lR && lR->undef_free) && (rR && rR->undef_free);
      // Wrapping multiplication cannot produce poison.
      if (!(nuw || nsw) || r.u || r.s)
        r.poison_free = (lR && lR->poison_free) && (rR && rR->poison_free);
      if (!r.u && !r.s && !r.well_defined() && (!r.bits || r.bits->isUnknown()))
        return std::nullopt;
      crossDerive(r);
      deriveBitsFromIntervals(r, bw);
      return r;
    }

    default:
      return std::nullopt;
    }
  }

  // casts
  if (auto *cast = llvm::dyn_cast<llvm::CastInst>(I)) {
    const KnownRange *srcR = rangeOf(cast->getOperand(0), map, srcS);
    if (!srcR)
      return std::nullopt;

    switch (cast->getOpcode()) {

    case llvm::Instruction::ZExt: {
      KnownRange r;
      if (srcR->u)
        r.u = {srcR->u->first.zext(bw), srcR->u->second.zext(bw)};
      else if (srcR->s && !srcR->s->first.isNegative())
        r.u = {srcR->s->first.zext(bw), srcR->s->second.zext(bw)};
      r.bits = knownBitsOf(cast->getOperand(0), map).zext(bw);
      if (!r.u && (!r.bits || r.bits->isUnknown()))
        return std::nullopt;
      r.undef_free = srcR->undef_free;
      r.poison_free = srcR->poison_free;
      crossDerive(r);
      deriveBitsFromIntervals(r, bw);
      return r;
    }

    case llvm::Instruction::SExt: {
      KnownRange r;
      if (srcR->s)
        r.s = {srcR->s->first.sext(bw), srcR->s->second.sext(bw)};
      else if (srcR->u && !srcR->u->second.isNegative())
        r.s = {srcR->u->first.sext(bw), srcR->u->second.sext(bw)};
      r.bits = knownBitsOf(cast->getOperand(0), map).sext(bw);
      if (!r.s && (!r.bits || r.bits->isUnknown()))
        return std::nullopt;
      r.undef_free = srcR->undef_free;
      r.poison_free = srcR->poison_free;
      crossDerive(r);
      deriveBitsFromIntervals(r, bw);
      return r;
    }

    case llvm::Instruction::Trunc: {
      KnownRange r;
      if (srcR->u && srcR->u->second.getActiveBits() <= bw)
        r.u = {srcR->u->first.trunc(bw), srcR->u->second.trunc(bw)};
      r.bits = knownBitsOf(cast->getOperand(0), map).trunc(bw);
      if (!r.u && (!r.bits || r.bits->isUnknown()))
        return std::nullopt;
      r.undef_free = srcR->undef_free;
      r.poison_free = srcR->poison_free;
      crossDerive(r);
      deriveBitsFromIntervals(r, bw);
      return r;
    }

    default:
      return std::nullopt;
    }
  }

  return std::nullopt;
}

} // namespace

RangeMap computeRanges(llvm::ArrayRef<llvm::Instruction *> insts,
                       const RangeMap &seed) {
  RangeMap map = seed;
  for (llvm::Instruction *I : insts) {
    auto r = transfer(I, map);
    if (r)
      map[I] = std::move(*r);
  }
  return map;
}

} // namespace alive_tv_next
