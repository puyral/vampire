/*
 * This file is part of the source code of the software program
 * Vampire. It is protected by applicable
 * copyright laws.
 *
 * This source code is distributed under the licence found here
 * https://vprover.github.io/license.html
 * and in the source directory
 */

namespace AdditionGeneralizationImpl {

/**
 * Rule' 2)
 *   generalize multiplication
 *   C[X + d] 
 *   ------------ 
 *   C[X]
 *   where 
 *   - all occurences of X are in terms of the form `X + d`
 *   - sound due to substitution X -> X - d
 *   - generalization since obviously
 */

template<class NumTraits> class MonomSet;

using GenMap = Map<Variable, AnyNumber<MonomSet>, StlHash>;

template<class NumTraits>
class MonomSet 
{
  using Monom = Kernel::Monom<NumTraits>;
  using Const = typename NumTraits::ConstantType;
  using MonomFactors = Kernel::MonomFactors<NumTraits>;

  Stack<Monom> _cancellable;

  MonomSet(decltype(_cancellable) cancel) : _cancellable(cancel) {}

public:
  using Lattice = MonomSet;
  MonomSet& operator=(MonomSet&&) = default;
  MonomSet(MonomSet&&) = default;

  static MonomSet bot() 
  { return MonomSet(decltype(_cancellable){}); }

  MonomSet(Variable var, Polynom<NumTraits> poly) : MonomSet(decltype(_cancellable)()) 
  {
    _cancellable.reserve(poly.cntSummands() - 1);
    for (auto const& monom : poly.iterSummands()) {
      if (monom.tryVar() != some(var)) {
        _cancellable.push(monom);
      }
    }
  }

  MonomSet intersect(MonomSet&& rhs) && {
    CALL("MonomSet::intersect")
    auto& lhs = *this;
    return MonomSet(intersectSortedStack(std::move(lhs._cancellable), std::move(rhs._cancellable)));
  }

  Stack<Monom> const& summands() const 
  { return _cancellable; }

  bool isBot() const 
  { return _cancellable.isEmpty(); }

  friend ostream& operator<<(ostream& out, MonomSet const& self)
  { return out << self._cancellable; }
};


struct Preprocess 
{
  GenMap& map;

  template<class NumTraits> 
  void operator()(Polynom<NumTraits> const& poly)
  {
    CALL("AdditionGeneralizationImpl::Preprocess::operator()")
    // a variable might occur twice within one sum.
    Set<Variable, StlHash> didOccur;
    DBGE(poly)
    for (auto monom : poly.iterSummands()) {
    DBGE(monom)
      auto var = monom.tryVar();

      if (var.isSome() && !didOccur.contains(var.unwrap())) {
        auto v = var.unwrap();
        didOccur.insert(v);
        auto gen = MonomSet<NumTraits>(v, poly);
        map.updateOrInit(v,
            [&](AnyNumber<MonomSet> old_) 
            { 
              auto old = old_.downcast<NumTraits>().unwrap();
              auto result = move(old).intersect(move(gen));
              return AnyNumber<MonomSet>(move(result)); 
            },
            [&]() { return AnyNumber<MonomSet>(move(gen)); });
      } else {
        for (auto factor : monom.factors.iter()) {
           if (factor.term().isVar()) {
             auto v = factor.term().unwrapVar();
             map.replaceOrInsert(v, MonomSet<NumTraits>::bot());
           }
        }
      }

    }
  }
};


/** 
 * A polymorphic closure to bottom-up evaluate clause bottom-up that replaces all occurences of the factors in the field `toRem`
 */
struct Generalize 
{
  Variable var;
  AnyNumber<MonomSet>& gen;
  bool doOrderingCheck;

  template<class NumTraits>
  Polynom<NumTraits> operator()(Polynom<NumTraits> poly, PolyNf* generalizedArgs)  
  {
    CALL("AdditionGeneralizationImpl::Generalize::operator()")
    using Monom = Kernel::Monom<NumTraits>;

    auto found = poly.iterSummands()
      .find([&](Monom p) 
          { return p.tryVar() == some(var); });
    if (found.isNone()) {
      return poly.replaceTerms(generalizedArgs);
    }

    auto& toCancel = gen.downcast<NumTraits>().unwrap().summands();


    Stack<Monom> out(poly.cntSummands() - toCancel.size());

    auto summandIter = poly.iterSummands();
    ASS(summandIter.hasNext())
    auto cur = summandIter.tryNext();
    unsigned genOffs = 0;

    auto pushGeneralized = [&]()
    {
      // TODO make replaceTerms return the number of factors
      unsigned cnt;
      auto factors = cur.unwrap().factors.replaceTerms(&generalizedArgs[genOffs], cnt);
      auto coeff = cur.unwrap().numeral;

      genOffs += cnt;
      cur = summandIter.tryNext();

      return out.push(Monom(coeff, std::move(factors)));
    };

    auto skipGeneralized = [&]() 
    { 
      genOffs += cur.unwrap().factors.cntFactors(); 
      cur = summandIter.tryNext();
    };

    unsigned c = 0; 
    while (c < toCancel.size() && cur.unwrap() < toCancel[c]  ) {
      pushGeneralized();
    }
    while (cur.isSome() && c < toCancel.size()) {
      if (cur.unwrap() == toCancel[c]) {
        skipGeneralized();
        c++;
      } else {
        ASS_L(cur.unwrap(), toCancel[c]);
        pushGeneralized();
      }
    }
    while (cur.isSome()) {
      pushGeneralized();
    }

    return Polynom<NumTraits>(std::move(out));
  }
};


struct IsBot 
{
  template<class C>
  bool operator()(C const& lattice)
  { return lattice.isBot(); }
};

/** applies the rule */ 
SimplifyingGeneratingInference1::Result applyRule(Clause* cl, bool doOrderingCheck) 
{
  DEBUG("input clause: ", *cl);

  GenMap map;

  for (auto poly : iterPolynoms(cl)) {
    poly.apply(Preprocess {map});
  }

  Option<typename GenMap::Entry &> selected;
  for (auto& e : iterTraits(map.iter()) ) {
    if (!e.value().apply(IsBot{})) {
      /* we use the entry with the least variable, in order to prevent non-determinism */
      if (selected.isNone() || e.key() < selected.unwrap().key()) {
        selected = decltype(selected)(e);
      }
    }
  }

  if (selected.isNone()) {
    DEBUG("not applicable")
    return SimplifyingGeneratingInference1::Result::nop(cl);
  } else {
    auto& e = selected.unwrap();
    DEBUG("selected generalization: ", e.key(), " ", e.value());
    Generalize gen { e.key(), e.value(), doOrderingCheck };
    auto out = generalizeBottomUp(cl, EvaluatePolynom<Generalize> {gen});
    DEBUG("result: ", *out.simplified);
    return out;
  }

}

} // namespace AdditionGeneralizationImpl
