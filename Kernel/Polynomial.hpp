#ifndef __POLYNOMIAL__H__
#define __POLYNOMIAL__H__

#include "Lib/STLAllocator.hpp"
#include "Kernel/NumTraits.hpp"
#include <vector>
#include "Lib/Coproduct.hpp"
#include "Lib/Optional.hpp"
#include "Lib/Map.hpp"
#include "Kernel/Theory.hpp"
#include <map> // TODO replace by Map

#define DEBUG(...) //DBG(__VA_ARGS__)


namespace Kernel {

/** 
 * Smart pointer for aggressively shared objects.
 * TODO document, factor out.
 */
template<class T>
class Shared 
{
  T* _elem;


  explicit Shared(T&& t) : _elem(_cached.getOrInit(T(t), [t]() { return new T(std::move(t)); })) 
      // return monoms.getOrInit(MonomInner(self),
      //     [=]() { return new MonomInner(std::move(self)); });
  { }
public:
  struct Hasher;
private:
  using Cache = Map<T, T*, StlHash<T>>;
  static Cache _cached;
public:

  Shared() : _elem(nullptr) {}

  // const T operator*()

  template<class U>
  friend bool operator==(Shared<U> const& l, Shared<U> const& r)
  { return l._elem == r._elem; }

  template<class U>
  friend bool operator!=(Shared<U> const& l, Shared<U> const& r)
  { return l != r; }

  T* operator->() const { return _elem; }

  T const& operator*() const& { return *_elem; }
  T      & operator*()      & { return *_elem; }
  T     && operator*()     && { return *_elem; }

  operator T const&() const& { return *_elem; }
  operator T      &()      & { return *_elem; }
  operator T     &&()     && { return *_elem; }

  friend ostream& operator<<(ostream& out, Shared& self) 
  { return out << *self._elem; }

  template<class U>
  friend Shared<U> shared(U&& t);

public:
  struct Hasher {
    static bool equals(Shared<T> const& l, Shared<T> const& r) { return l._elem == r._elem; }
    static unsigned hash(const Shared<T>& self) { return std::hash<size_t>{}((size_t)self._elem); }
  };
};

template<class T> typename Shared<T>::Cache Shared<T>::_cached;

template<class T>
Shared<T> shared(T&& t) { return Shared<T>(std::move(t)); }



template<class t> using vector  = std::vector<t, Lib::STLAllocator<t>>;
template<class K, class V, class Compare = std::less<K>> using map  = std::map<K, V, Compare, STLAllocator<std::pair<const K, V > > >;

/** Merges two map-like vectors into a new map-like vector. 
 * A vector is map-like if has key-value pairs as entry, is sorted by keys and each key is unique within the vector.
 *
 * If there is a key present in both lhs and rhs, the corresponding the two corresponding values will be combined 
 * with the closure @b add. After that the result of combining is then used as argument for @b filter() and will 
 * be discarded if filter returns false.
 */
template<class A, class B, class Add, class Filter>
vector<tuple<A, B>> merge_sort_with(const vector<tuple<A, B>>& lhs, const vector<tuple<A, B>>& rhs, Add add, Filter filter) {
    CALL("merge_sort_with()")

    vector<tuple<A,B>> out;
    /* is needed at least */
    out.reserve(max(lhs.size(), rhs.size()));
    auto l = lhs.begin();
    auto r = rhs.begin();
    auto insert = [&](const A& key, B value) {
      ASS(filter(value));
      out.emplace_back(make_tuple(A(key), value));
    };
    auto fst = [](const tuple<A,B>& x) { return get<0>(x); };
    auto snd = [](const tuple<A,B>& x) { return get<1>(x); };
    while (l != lhs.end() && r != rhs.end() ) {
      if (fst(*l) == fst(*r)) {
        //add up
        auto sum = add(snd(*l), snd(*r));
        if (filter(sum))
          insert(fst(*l), sum);
        l++;
        r++;
      } else if (fst(*l)< fst(*r)) {
        // insert l
        insert(fst(*l), snd(*l));
        l++;
      } else {
        // insert r
        insert(fst(*r), snd(*r));
        r++;
      }
    }
    while (l != lhs.end()) {
      insert(fst(*l), snd(*l));
      l++;
    }
    while (r != rhs.end()) {
      insert(fst(*r), snd(*r));
      r++;
    }
    ASS(l == lhs.end() && r == rhs.end());
    return std::move(out);
}


template<class number>
class Monom { 
public:
  using Coeff = typename number::ConstantType;
  class MonomInner;
  struct Hasher;
  Monom& operator=(const Monom&) = default;
  Monom(Monom&&) = default;

private:
  MonomInner* _inner;
  using Cache = Map<MonomInner, MonomInner*, Hasher>;
  static Cache monoms;

public:

  bool isOne() const {return _inner->isOne();}

  template<class Config>
  TermList toTerm() {return _inner->template toTerm<Config>();}

  friend bool operator<(const Monom& lhs, const Monom& rhs) { return lhs._inner < rhs._inner; }
  friend bool operator>(const Monom& lhs, const Monom& rhs) { return lhs._inner > rhs._inner; }

  friend bool operator==(const Monom& lhs, const Monom& rhs) {return lhs._inner == rhs._inner;}
  size_t hash() const { return std::hash<size_t>{}((size_t) _inner); }

  friend ostream& operator<<(ostream& out, const Monom& m) {return out << *m._inner;}

  Monom(const Monom& other) : _inner(other._inner) {}
  Monom& operator=(Monom&& other) = default;  
  Monom(MonomInner* inner) : _inner(inner) {}
  Monom() : _inner(MonomInner::create(MonomInner())) {}

  Monom(TermList t) : _inner(MonomInner::create(MonomInner(t))) {}
  Monom(TermList factor1, TermList factor2) : _inner(MonomInner::create(MonomInner(factor1, factor2))) { }


  static Monom monom_mul(const Monom& lhs, const Monom& rhs) {
    return Monom(MonomInner::monom_mul(*lhs._inner, *rhs._inner));
  }

  // Monom& operator=(Monom&&) = default;
  class MonomInner {
    vector<tuple<TermList, int>> _factors;
    Lib::Optional<TermList> _toTerm;
    friend class Monom;

    // empty monom == 1
    static MonomInner* create(MonomInner&& self) {
      CALL("MonomInner::create(MonomInner&&)")
      return monoms.getOrInit(MonomInner(self),
          [=]() { return new MonomInner(std::move(self)); });
          // [=](MonomInner** toInit) {*toInit = new MonomInner(std::move(self));});
    }

  public:
    MonomInner() : _factors(decltype(_factors)()) { }
    private:

    MonomInner(decltype(_factors) factors) : _factors(factors) { }

    MonomInner(TermList t) : _factors { make_tuple(t, 1)}  { }
    MonomInner(TermList t1, TermList t2) 
      : _factors(t1 == t2 ? decltype(_factors) ({ make_tuple(t1, 2)}) : 
                 t1 <  t2 ? decltype(_factors) ({ make_tuple(t1,1), make_tuple(t2,1)}) :
                            decltype(_factors) ({ make_tuple(t2,1), make_tuple(t1,1)}) 
                            )  { }

    public:

      USE_ALLOCATOR(MonomInner)
      CLASS_NAME(MonomInner)
      using monom_pair = typename decltype(_factors)::value_type;

    static TermList getTerm(const typename decltype(_factors)::value_type& pair) {
      return std::get<0>(pair);
    }

    static int getCount(const typename decltype(_factors)::value_type& pair) {
      return std::get<1>(pair);
    }

    bool isOne() const  {
      return _factors.begin() == _factors.end();
    }

    static TermList pairToTerm(const monom_pair& pair) {
      auto cnt = getCount(pair);
      ASS_REP(cnt > 0, cnt)

      auto trm = getTerm(pair);
      auto out = trm;
      for (auto i = 1; i < cnt; i++) {
        out = number::mul(trm, out);
      }
      return out;
    }

    template<class Config>
    TermList toTerm() {
      CALL("MonomInner::toTerm()")
      return _toTerm.unwrapOrInit([&]() {

        if (_factors.size() == 0) {
          return number::one();
        } else {

          vector<TermList> factors;
          auto sz = 0;
          for(auto& f : _factors) {
            sz += getCount(f);
          }
          factors.reserve(sz);

          for (auto f : _factors) {
            for (auto i = 0; i < getCount(f); i++) {
              factors.push_back(getTerm(f));
            }
          }

          sort(begin(factors), end(factors), typename Config::Ordering{});

          auto iter = factors.rbegin();

          auto out = *iter;
          iter++;
          for(; iter != factors.rend(); iter++)  {
            out = number::mul(*iter, out); 
          }
          return out;
        }
      });
    }

    friend std::ostream& operator<<(std::ostream& out, const MonomInner& self) {
      if (self._factors.size() == 0) {
        return out << "1";
      } else {
        auto iter  = self._factors.begin();
        out << getTerm(*iter) << "^" << getCount(*iter);
        iter++;
        for (; iter != self._factors.end(); iter++) {
          out << " * " << getTerm(*iter) << "^" << getCount(*iter);
        }
        return out;
      }
    }

    friend bool operator<(const MonomInner& l, const MonomInner& r) {
      if (l._factors.size() < r._factors.size()) {
        return true;
      } else if (l._factors.size() > r._factors.size()) {
        return false;
      } else {
        return  l._factors < r._factors;
      }
    }

    friend bool operator>(const MonomInner& l, const MonomInner& r) { return r < l; }

    friend bool operator==(const MonomInner& l, const MonomInner& r) {
      return l._factors == r._factors;
    }

    public:

    MonomInner& operator=(MonomInner&&) = default;
    MonomInner(MonomInner&&) = default;

    static MonomInner* monom_mul(const MonomInner& lhs, const MonomInner& rhs) {
      return MonomInner::create(MonomInner(merge_sort_with(lhs._factors,rhs._factors,
             [](int l, int r) { return l + r; },
             [](int l) { return l != 0; })));
    }

    explicit MonomInner(const MonomInner&) = default;
    explicit MonomInner(MonomInner&) = default;
  };
public:
  struct Hasher {

    static unsigned hash(Monom::MonomInner const& x) noexcept {
      unsigned out = 0;
      for (auto f : x._factors) {
        out ^= TermListHash::hash(std::get<0>(f));
        out ^= std::hash<int>{}(std::get<1>(f));
        out <<= 1;
      }
      return out;
    }

    static bool equals(Monom::MonomInner const& lhs, Monom::MonomInner const& rhs) noexcept {
      return lhs == rhs;
    }
  };
};

template<class number>
class ComplexPolynom 
{
  friend struct std::hash<ComplexPolynom>;
  template<class NumTraits> friend class Polynom;

  using Coeff = typename number::ConstantType;
  using PMonom = Monom<number>;
public:
  USE_ALLOCATOR(ComplexPolynom)
  CLASS_NAME(ComplexPolynom)

private:
  using CoeffVec = vector<tuple<PMonom, Coeff>>;
  CoeffVec _coeffs;
  Lib::Optional<TermList> _toTerm;
  using poly_pair = typename decltype(_coeffs)::value_type;

public:

  ComplexPolynom(Coeff coeff, PMonom&& t) : _coeffs(decltype(_coeffs)())  { 
    _coeffs.emplace_back(poly_pair(std::move(t), coeff));
  }

  ComplexPolynom(PMonom&& t) : _coeffs(decltype(_coeffs)())  { 
    _coeffs.emplace_back(poly_pair(std::move(t), Coeff(1)));
  }

  ComplexPolynom(Coeff coeff, TermList t) : ComplexPolynom(coeff, PMonom(t))  { 
    // _coeffs.emplace_back(poly_pair(PMonom(t), coeff));
  }

  ComplexPolynom(Coeff constant) : _coeffs(decltype(_coeffs)())  { 
    CALL("ComplexPolynom::ComplexPolynom(Coeff)")
    if (constant != number::zeroC)
      _coeffs.emplace_back(poly_pair(PMonom(), constant));
  }

  ComplexPolynom(decltype(_coeffs) coeffs) : _coeffs(coeffs) { }

  ComplexPolynom() : _coeffs(decltype(_coeffs)()) {
  }

  ComplexPolynom(ComplexPolynom&& other) = default;
  explicit ComplexPolynom(const ComplexPolynom&) = default;
  unsigned nSummands() const& { return _coeffs.size(); }

  ComplexPolynom& operator=(ComplexPolynom&& other) = default;


  friend bool operator==(const ComplexPolynom& lhs, const ComplexPolynom& rhs) {
    return lhs._coeffs == rhs._coeffs;
  }

  static PMonom& getMonom(poly_pair& pair) {
    return std::get<0>(pair);
  }

  static const PMonom& getMonom(const poly_pair& pair) {
    return std::get<0>(pair);
  }

  static const Coeff& getCoeff(const poly_pair& pair) {
    return std::get<1>(pair);
  }

  static Coeff& getCoeffMut(poly_pair& pair) {
    return std::get<1>(pair);
  }

  void integrity() const {
#if VDEBUG
    if (_coeffs.size() > 0) {
      auto iter = this->_coeffs.begin();
      auto last = iter++;
      while (iter != _coeffs.end()) {
        ASS_REP(getMonom(*last) < getMonom(*iter), *this);
        last = iter++;
      }
    }
#endif
  }

  template<class Config>
  static TermList toTerm(ComplexPolynom& self) {
    CALL("ComplexPolynom::toTerm() const")
    return self._toTerm.unwrapOrInit([&]() {
      // self.integrity();
      
      auto trm = [](poly_pair& x) -> TermList { 

        if (getMonom(x).isOne()) {  
          /* the pair is a plain number */
          return TermList( theory->representConstant(getCoeff(x)) );

        } else if (getCoeff(x)== number::constant(1)) {
          /* the pair is an uninterpreted term */
          return getMonom(x).template toTerm<Config>();

        } else if (getCoeff(x)== number::constant(-1)) {
          return TermList(number::minus(getMonom(x).template toTerm<Config>()));

        } else {
          return TermList(number::mul(TermList( theory->representConstant(getCoeff(x)) ), getMonom(x).template toTerm<Config>())); 
        }
      };

      vector<TermList> coeffs(self._coeffs.size());
      transform(begin(self._coeffs),end(self._coeffs), begin(coeffs), trm);

      sort(begin(coeffs), end(coeffs), typename Config::Ordering{});

      auto iter = coeffs.rbegin(); 
      if (iter == coeffs.rend()) {
        return TermList(number::zero());
      } else {
        auto out = *iter;
        iter++;
        for (; iter != coeffs.rend(); iter++) {
          out = number::add(*iter, out);
        }
        return out;
      }
    });
  }

  friend std::ostream& operator<<(std::ostream& out, const ComplexPolynom& self) {
    auto iter = self._coeffs.begin();
    if ( iter == self._coeffs.end() ) {
      out << "0";
    } else {
      out << getMonom(*iter)<< " * " << getCoeff(*iter);
      iter++;
      for (; iter != self._coeffs.end(); iter++) {
        out << " + " << getMonom(*iter)<< " * " << getCoeff(*iter);
      }
    }
    return out;
  }

};

/**
 * A polynomial of a specific interpreted number sort. The type parameter is expected to be an instance of NumTraits<...>, 
 * defined in NumTraits.hpp.
 */
template<class number>
class Polynom 
{
public:
  using Coeff = typename number::ConstantType;
  using PMonom = Monom<number>;
  using poly_pair = typename ComplexPolynom<number>::poly_pair;

private:
  friend struct std::hash<Polynom>;
  friend struct std::hash<ComplexPolynom<number>>;


  // optimization in order to make handle polynomials that are only a single number more efficiently
  using Inner = Coproduct<Shared<ComplexPolynom<number>>, Coeff>;
  Inner _inner;

  bool                      isComplex() const& { return _inner.template is<0>(); }
  const Shared<ComplexPolynom<number>>& unwrapComplex() const& { return _inner.template unwrap<0>(); }
        Shared<ComplexPolynom<number>>& unwrapComplex()      & { return _inner.template unwrap<0>(); }

public:
  friend bool operator==(Polynom const& lhs, Polynom const& rhs) 
  { return lhs._inner == rhs._inner; }

  // TODO rename to isNumber
  bool isCoeff() const& { return _inner.template is<1>(); }
  // TODO rename to unwrapNumber
  Coeff unwrapCoeff() const& { return _inner.template unwrap<1>(); }

  friend ostream& operator<<(ostream& out, const Polynom& self) { 
    self._inner.template match<void>(
          [&](Shared<ComplexPolynom<number>> poly) { out << poly; }
        , [&](Coeff self          ) { out << self; }
        );
    return out;
  }

#define DBGE(x) //DBG(#x " = ", x)

private:

  static Polynom<number> poly_add(const ComplexPolynom<number>& lhs, const ComplexPolynom<number>& rhs) {
    CALL("ComplexPolynom::poly_add")
    // DBG("lhs: ", lhs)
    // DBG("rhs: ", rhs)
    ASS(!lhs._coeffs.empty())
    ASS(!rhs._coeffs.empty())
    auto newCoeffs = merge_sort_with(lhs._coeffs, rhs._coeffs, 
            [](Coeff l, Coeff r){ return l + r; },
            [](Coeff x){ return x != number::zeroC; }
          );
    if (newCoeffs.empty())  {
      return Polynom(Coeff(0));
    } else {
      return Polynom(shared(ComplexPolynom<number>(std::move(newCoeffs))));
    }
  }

  inline static Shared<ComplexPolynom<number>> add(Coeff coeff, Shared<ComplexPolynom<number>> old_) {
    CALL("ComplexPolynom::add(Coeff coeff, const ComplexPolynom& old) ")
    const auto& oldPoly = *old_;

    ASS(!oldPoly._coeffs.empty())
    if (coeff == Coeff(0)) {
      return old_;
    } 

    ComplexPolynom<number> newPoly;
    if (oldPoly.getMonom(oldPoly._coeffs[0]).isOne()) {
      ASS(oldPoly._coeffs.begin() != oldPoly._coeffs.end())

      auto newVal = oldPoly.getCoeff(oldPoly._coeffs[0]) + coeff;
      if (newVal == Coeff(0)) {
        /* skipping zero constant value */
        newPoly._coeffs.reserve(oldPoly._coeffs.size() - 1);
        
        auto iter = oldPoly._coeffs.begin();
        iter++;
        for (; iter !=  oldPoly._coeffs.end(); iter++) {
          newPoly._coeffs.emplace_back(poly_pair(*iter));
        }
      } else {
        /* skipping zero constant value */
        newPoly._coeffs = oldPoly._coeffs;
        oldPoly.getCoeffMut(newPoly._coeffs[0]) = newVal;
      }
    } else {
      newPoly._coeffs.reserve(oldPoly._coeffs.size() + 1);
      newPoly._coeffs.push_back(poly_pair(PMonom(), coeff));
      for (auto& f : oldPoly._coeffs) {
        // newPoly.push_back(poly_pair(PMonom(old.getMonom(p), old.getMonom())))
        newPoly._coeffs.push_back(poly_pair(f));
      }
    }

    // DBG("in : ", oldPoly, "\t+\t", coeff)
    // DBG("out: ", newPoly)

    return shared(std::move(newPoly));
  }

  static Polynom<number> coeff_poly_mul(Coeff coeff, const Shared<ComplexPolynom<number>>& old_) {
    CALL("ComplexPolynom::coeff_poly_mul(Coeff coeff, Shared<ComplexPolynom> old) ")
    auto& old = *old_;

    if (coeff == Coeff(0)) {
      return Polynom(Coeff(0));

    } else if (coeff == Coeff(1)) {
      return Polynom(old_);

    } else {
      ComplexPolynom<number> newPoly;

      newPoly._coeffs.reserve(old._coeffs.size());
      for (auto& p : old._coeffs) {
        newPoly._coeffs.push_back(poly_pair(PMonom(old.getMonom(p)), coeff * old.getCoeff(p)));
      }

      return Polynom(shared(std::move(newPoly)));
    }
  }

  static Shared<ComplexPolynom<number>> poly_mul(const ComplexPolynom<number>& lhs, const ComplexPolynom<number>& rhs) {

    CALL("ComplexPolynom::poly_mul");
    DEBUG("lhs: ", lhs);
    DEBUG("rhs: ", rhs);

    //TODO use Map instead
    map<PMonom, Coeff> prods;

    for (auto& l : lhs._coeffs) {
      for (auto& r : rhs._coeffs) {
        PMonom monom = PMonom::monom_mul( lhs.getMonom(l), rhs.getMonom(r));
        auto coeff = lhs.getCoeff(l) * rhs.getCoeff(r);
        auto res = prods.emplace(make_pair(move(monom), coeff));
        if (!res.second) {
          auto& iter = res.first;
          ASS(iter != prods.end());
          iter->second = iter->second + coeff;
        }
      }
    }
    ComplexPolynom<number> out;
    out._coeffs.reserve(prods.size());
    for (auto iter = prods.begin(); iter != prods.end(); iter++) {
      auto coeff = iter->second;
      if (coeff != number::zeroC) {
        out._coeffs.emplace_back(poly_pair(iter->first, coeff)); 
      }
    }
    DEBUG("out: ", out);
    out.integrity();
    return shared(std::move(out));
  }

  static std::pair<Polynom, Polynom> cancel_(Coeff oldl, Coeff oldr) {
    auto zero = number::zeroC;
    if (oldl >= zero && oldr >= zero) {
      // cancelation simplifies:
      //    10 ~~  8 ==> 2 ~~ 0 
      if (oldl > oldr) {
        return make_pair(Polynom(oldl - oldr), Polynom(zero));
      } else {
        return make_pair(Polynom(zero), Polynom(oldr - oldl));
      }
    } else if (oldl < zero && oldr < zero) {
      // cancelation simplifies:
      //   -10 ~~ -8 ==> 0 ~~ 2 
      if (oldl < oldr) {
        return make_pair(Polynom(zero), Polynom(oldr - oldl));
      } else {
        return make_pair(Polynom(oldl - oldr), Polynom(zero));
      }
    } else {
      // cancelation does not simplify.
      //   -10 ~~  8 ==> 0 ~~ 18  
      //    10 ~~ -8 ==> 18 ~~ 0  
      return make_pair(Polynom(oldl),Polynom(oldr));
    }
  }

  static std::pair<Polynom, Polynom> cancel_(Coeff oldl, Shared<ComplexPolynom<number>>& oldr_) {
    auto& oldr = *oldr_;

    auto fstCoeff = oldr._coeffs[0];
    if (!oldr.getMonom(fstCoeff).isOne()) {
      // oldr does not contain a constant term
      return make_pair(Polynom(oldl), Polynom(oldr_));
    } 

    auto numr = oldr.getCoeff(fstCoeff);
    // auto zero = number::zeroC;
    // auto sameSign = (oldl <= zero) == (numr <= zero);

    //   consider: 
    //   -10 + x ~~  8 ==> -18 + x ~~ 0
    //            OR   ==>       x ~~ 18
    //            both cases do not simplify wrt KBO
    //
    // TODO resolve this strictly non-simplifying behaviour
    //      same applies to cancel_(ComplexPolynom&, ComplexPolynom& oldl)

    return make_pair(Polynom(oldl - numr), Polynom(shared(ComplexPolynom<number>(typename ComplexPolynom<number>::CoeffVec(++oldr._coeffs.begin(), oldr._coeffs.end())))));
  }

  static std::pair<Polynom, Polynom> cancel_(Shared<ComplexPolynom<number>>& oldl, Coeff oldr) {
    auto flipped = cancel_(oldr, oldl);
    return make_pair(std::move(get<1>(flipped)), std::move(get<0>(flipped)));
  }

  static std::pair<Polynom, Polynom> cancel_(Shared<ComplexPolynom<number>>& oldl_, Shared<ComplexPolynom<number>>& oldr_) {
    auto& oldl = *oldl_;
    auto& oldr = *oldr_;
    using CoeffVec = typename ComplexPolynom<number>::CoeffVec;
    auto zero = number::zeroC;
    auto itl = oldl._coeffs.begin();
    auto itr = oldr._coeffs.begin();
    auto endl = oldl._coeffs.end();
    auto endr = oldr._coeffs.end();
    auto push = [](CoeffVec& vec, const PMonom& m, Coeff c) {
      vec.emplace_back(make_pair(PMonom(m), c));
    };
    CoeffVec newl;
    CoeffVec newr;
    while(itl != endl && itr !=  endr) {
      auto cl = oldl.getCoeff(*itl);
      auto cr = oldr.getCoeff(*itr);
      auto& ml = oldl.getMonom(*itl);
      auto& mr = oldr.getMonom(*itr);
      if (ml == mr) {
        auto& m = ml;
        ASS_NEQ(cl, zero);
        ASS_NEQ(cr, zero);
        if (cl == cr) {
          // 10 x + ... ~~  10 x + ... ==> ... ~~ ... 
        } else if (cl > zero && cr > zero) {
          // 10 x + ... ~~  8 x + ... ==> 2 x + ... ~~ ... 
          if  ( cl > cr ) {
            push(newl, m, cl - cr);
          } else {
            push(newr, m, cr - cl);
          }
        } else if (cl < zero && cr < zero) {
          // -10 x + ... ~~  -8 x + ... ==> -2 x + ... ~~ ... 
          if  ( cl < cr ) {
            push(newl, m, cl - cr);
          } else {
            push(newr, m, cr - cl);
          }
        } else {
          if (cl < zero) {
            // -10 x + ... ~~  8 x + ... ==> ... ~~ 18 x + ... 
            push(newr, m, cr - cl);
          } else {
            //  10 x + ... ~~ -8 x + ... ==> 18 x + ... ~~ ... 
            push(newl, m, cl - cr);
          }
        }
        itl++;
        itr++;
        //TODO
      } else if (ml < mr) {
        push(newl, ml, cl);
        itl++;
      } else {
        ASS(ml > mr)
        push(newr, mr, cr);
        itr++;
      }
    }
    for(; itl != endl; itl++) {
      push(newl, get<0>(*itl), get<1>(*itl));
    }
    for(; itr != endr; itr++) {
      push(newr, get<0>(*itr), get<1>(*itr));
    }
    return make_pair(
        Polynom(shared(ComplexPolynom<number>(std::move(newl)))),
        Polynom(shared(ComplexPolynom<number>(std::move(newr))))
      ); 
  }

public:

  static std::pair<Polynom, Polynom> cancel(Polynom& lhs, Polynom& rhs) {
    // only dispatiching is going on here
    DBGE(lhs)
    DBGE(rhs)
    if (lhs.isCoeff()) {
      if (rhs.isCoeff()) {
        return cancel_(lhs.unwrapCoeff(), rhs.unwrapCoeff());
      } else {
        return cancel_(lhs.unwrapCoeff(), rhs.unwrapComplex());
      }
    } else {
      ASS(lhs.isComplex())
      if (rhs.isCoeff()) {
        return cancel_(lhs.unwrapComplex(), rhs.unwrapCoeff());
      } else {
        return cancel_(lhs.unwrapComplex(), rhs.unwrapComplex());
      }
    }
  }

  template<class Config>
  TermList toTerm() { 
    return _inner.template match<TermList>(
          [](Shared<ComplexPolynom<number>> self) { return ComplexPolynom<number>::template toTerm<Config>(self); }
        , [](Coeff self          ) { return TermList(theory->representConstant(self)); }
        );
  }

  template<class Config>
  static TermList toTerm(Polynom& self) { 
    return self._inner.template match<TermList>(
          [](Shared<ComplexPolynom<number>> self) { return ComplexPolynom<number>::template toTerm<Config>(self); }
        , [](Coeff self          ) { return TermList(theory->representConstant(self)); }
        );
  }

public:

  template<class Config>
  inline static Polynom poly_mul(Polynom& lhs, Polynom& rhs) 
  {
    return lhs._inner.template match<Polynom>(
          [&](Shared<ComplexPolynom<number>> & lhs) { 
            return rhs._inner.template match<Polynom>(
                  [&](Shared<ComplexPolynom<number>> & rhs) { 
                    if(Config::usePolyMul || (lhs->nSummands() == 1 && rhs->nSummands() == 1 )) {
                      return Polynom(poly_mul(lhs, rhs)); 
                    } else {
                      auto l = ComplexPolynom<number>::template toTerm<Config>(*lhs);
                      auto r = ComplexPolynom<number>::template toTerm<Config>(*rhs);
                      return Polynom(shared(ComplexPolynom<number>(PMonom(l,r))));
                    }
                  }
                , [&](Coeff           & rhs) { return coeff_poly_mul(rhs, lhs); }
                );
          }
        , [&](Coeff & lhs) { 
            return rhs._inner.template match<Polynom>(
                  [&](Shared<ComplexPolynom<number>> & rhs) { return coeff_poly_mul(lhs, rhs); }
                , [&](Coeff           & rhs) { return Polynom(lhs * rhs); }
                );
        });
  }

  inline static Polynom poly_add(const Polynom& lhs, const Polynom& rhs) {
    return lhs._inner.template match<Polynom>(
          [&](Shared<ComplexPolynom<number>> const& lhs) { 
            return rhs._inner.template match<Polynom>(
                  [&](Shared<ComplexPolynom<number>> const& rhs) { return poly_add(lhs, rhs); }
                , [&](Coeff           const& rhs) { return Polynom(add(rhs, lhs)); }
                );
          }
        , [&](Coeff const& lhs) { 
            return rhs._inner.template match<Polynom>(
                  [&](Shared<ComplexPolynom<number>> const& rhs) { return Polynom(add(lhs, rhs)); }
                , [&](Coeff           const& rhs) { return Polynom(lhs + rhs); }
                );
        });
  }

  Polynom(TermList t) : Polynom(Coeff(1), t) {}
  Polynom(Coeff coeff, TermList t) : _inner(Inner::template variant<0>(shared(ComplexPolynom<number>(coeff, t)))) {}
  explicit Polynom(Coeff constant)          : _inner(Inner::template variant<1>(constant)) {}
  explicit Polynom(Shared<ComplexPolynom<number>> inner)   : _inner(Inner::template variant<0>(inner)) {} 
  // explicit Polynom(ComplexPolynom<number>& inner)   : _inner(Inner::template variant<0>(inner)) {} 

private:
};

using AnyPolySuper = Coproduct< Polynom<NumTraits<IntegerConstantType>> , Polynom<NumTraits<RationalConstantType>> , Polynom<NumTraits<RealConstantType>> > ;
struct AnyPoly : public AnyPolySuper
{
  
  template<class C>
  using poly = Polynom<NumTraits<C>>;

  explicit AnyPoly(poly<IntegerConstantType>&& x) : Coproduct(variant<0>(std::move(x))) 
  { CALL("AnyPoly(Int)") }

  explicit AnyPoly(poly<RationalConstantType>&& x ) : Coproduct(variant<1>(std::move(x))) 
  { CALL("AnyPoly(Rat)") }

  explicit AnyPoly(poly<RealConstantType>&& x ) : Coproduct(variant<2>(std::move(x))) 
  { CALL("AnyPoly(Real)") }

  template<class Const, class Config>
  TermList toTerm() 
  {
    CALL("AnyPoly::toTerm")
    return poly<Const>::template toTerm<Config>(as<poly<Const>>());
  }



  template<class Config>
  TermList toTerm_() {
    CALL("AnyPoly::toTerm_")

    if (is<0>()) return toTerm<typename type<0>::value::Coeff, Config>();
    if (is<1>()) return toTerm<typename type<1>::value::Coeff, Config>();
    if (is<2>()) return toTerm<typename type<2>::value::Coeff, Config>();

    ASSERTION_VIOLATION
  }

  friend std::ostream& operator<<(std::ostream& out, const AnyPoly& self) {
    if (self.is<0>()) return out << self.unwrap<0>();
    if (self.is<1>()) return out << self.unwrap<1>();
    if (self.is<2>()) return out << self.unwrap<2>();
    ASSERTION_VIOLATION
  }

  AnyPoly& operator=(AnyPoly&&) = default;
  AnyPoly(AnyPoly&&) = default;
  explicit AnyPoly(const AnyPoly&) = default;
  friend struct std::hash<AnyPoly>;
private:
};


template<class Number> typename Monom  <Number>::Cache Monom  <Number>::monoms;


} // namespace Kernel


template<class NumTraits> struct std::hash<Kernel::Polynom<NumTraits>> 
{
  size_t operator()(Kernel::Polynom<NumTraits> const& self) const 
  { return std::hash<decltype(self._inner)>{}(self._inner); }
};


template<class T> struct std::hash<Kernel::Shared<T>> 
{
  size_t operator()(Kernel::Shared<T> const& self) const 
  { return Kernel::Shared<T>::Hasher::hash(self); }
};


template<> struct std::hash<Kernel::AnyPoly> 
{
  size_t operator()(Kernel::AnyPoly const& self) const 
  { return std::hash<Kernel::AnyPolySuper>{}(self); }
};

template<class NumTraits>
struct std::hash<Kernel::ComplexPolynom<NumTraits>> 
{
  size_t operator()(Kernel::ComplexPolynom<NumTraits> const& x) const noexcept 
  {
    using namespace Lib;
    using namespace Kernel;

    unsigned out = HashUtils::combine(0,0);
    for (auto c : x._coeffs) {
      out = HashUtils::combine(ComplexPolynom<NumTraits>::getMonom(c).hash(),
            HashUtils::combine(ComplexPolynom<NumTraits>::getCoeff(c).hash(),
               out));
    }
    return out;
  }
};



#undef DEBUG

#endif // __POLYNOMIAL__H__