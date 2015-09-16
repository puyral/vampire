/**
 * @file Substitution.cpp
 * Implements class Substitution.
 * @since 28/12/2007 Manchester, implemented from scratch
 */

#if VDEBUG
#  include "Lib/Int.hpp"
#  include "Term.hpp"
#endif

#include "Substitution.hpp"

namespace Kernel
{

/**
 * Bind @b v to @b t.
 * @pre @b v must previously be unbound
 */
void Substitution::bind(int v,Term* t)
{
  CALL("Substitution::bind(int,Term*)");
  TermList ts;
  ts.setTerm(t);
  bind(v,ts);
}

/**
 * Bind @b v to @b t.
 * @pre @b v must previously be unbound
 */
void Substitution::bind(int v,TermList t)
{
  CALL("Substitution::bind(int,TermList)");

  ALWAYS(_map.insert(v, t));
} // Substitution::bind

/**
 * Remove the binding for @b v from the substitution.
 * @pre @b v must previously be bound
 * @since 04/05/2006 Bellevue
 * @since 30/12/2007 Manchester
 */
void Substitution::unbind(int v)
{
  CALL("Substitution::unbind");

  ALWAYS(_map.remove(v));
} // Substitution::unbind

void Substitution::reset()
{
  CALL("Substitution::reset");

  _map.reset();
}

/**
 * Return result of application of the substitution to variable @c var
 *
 * This function is to allow use of the @c Substitution class in the
 * methods of the @c SubstHelper class for applying substitutions.
 */
TermList Substitution::apply(unsigned var)
{
  TermList res;
  if(!findBinding(var, res)) {
    res = TermList(var,false);
  }
  return res;
}

/**
 * If @c var is bound, assign bingind into @c res and return true.
 * Otherwise return false and do nothing.
 */
bool Substitution::findBinding(int var, TermList& res) const
{
  CALL("Substitution::findBinding");

  return _map.find(var, res);
} // Substitution::bound


#if VDEBUG
 vstring Substitution::toString() const
 {
   vstring result("[");
   VirtualIterator<std::pair<unsigned,TermList>> items = _map.items();
   bool first=true;
   while(items.hasNext()){
     std::pair<unsigned,TermList> item = items.next();
     if(!first){result+=",";}
     first=false;
     result += Lib::Int::toString(item.first) + " -> " + item.second.toString(); 
   }
   result += ']';
   return result;
 } // Substitution::toString()
#endif

}

