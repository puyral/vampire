/**
 * @file TermSharing.cpp
 * Implements class TermSharing.
 *
 * @since 28/12/2007 Manchester
 */

#include "../Kernel/Term.hpp"
#include "TermSharing.hpp"

using namespace Kernel;
using namespace Indexing;

/**
 * Initialise the term sharing structure.
 * @since 29/12/2007 Manchester
 */
TermSharing::TermSharing()
  : _totalTerms(0),
    _groundTerms(0),
    _totalLiterals(0),
    _groundLiterals(0),
    _literalInsertions(0),
    _termInsertions(0)
{
}

/**
 * Destroy the term sharing structure.
 * @since 29/12/2007 Manchester
 */
TermSharing::~TermSharing()
{
#if CHECK_LEAKS
  Set<Term*,TermSharing>::Iterator ts(_terms);
  while (ts.hasNext()) {
    ts.next()->destroy();
  }
  Set<Literal*,TermSharing>::Iterator ls(_literals);
  while (ls.hasNext()) {
    ls.next()->destroy();
  }
#endif
}

/**
 * Insert a new term in the index and return the result.
 * @since 28/12/2007 Manchester
 */
Term* TermSharing::insert(Term* t)
{
  CALL("TermSharing::insert(Term*)");

  ASS(! t->isLiteral());

  // normalise commutative terms
  if (t->commutative()) {
    ASS(t->arity() == 2);

    TermList* ts1 = t->args();
    TermList* ts2 = ts1->next();
    if (ts1->_content > ts2->_content) {
      unsigned c = ts1->_content;
      ts1->_content = ts2->_content;
      ts2->_content = c;
    }
  }

  _termInsertions++;
  Term* s = _terms.insert(t);
  string n = "";
  if (s == t) {
    unsigned weight = 1;
    unsigned vars = 0;
    for (TermList* tt = t->args(); ! tt->isEmpty(); tt = tt->next()) {
      if (tt->isVar()) {
	vars++;
	weight += 1;
      }
      else {
	ASS(tt->term()->shared());

	Term* r = tt->term();
	vars += r->vars();
	weight += r->weight();
      }
    }
    t->markShared();
    t->setVars(vars);
    t->setWeight(weight);
    _totalTerms++;
  }
  else {
    t->destroy();
  }
  return s;
} // TermSharing::insert

/**
 * Insert a new term in the index and return the result.
 * @since 28/12/2007 Manchester
 */
Literal* TermSharing::insert(Literal* t)
{
  CALL("TermSharing::insert(Literal*)");
  ASS(t->isLiteral());

  if (t->commutative()) {
    ASS(t->arity() == 2);

    TermList* ts1 = t->args();
    TermList* ts2 = ts1->next();
    if (ts1->_content > ts2->_content) {
      unsigned c = ts1->_content;
      ts1->_content = ts2->_content;
      ts2->_content = c;
    }
  }

  _literalInsertions++;
  Literal* s = _literals.insert(t);
  string n = "";
  if (s == t) {
    unsigned weight = 1;
    unsigned vars = 0;
    for (TermList* tt = t->args(); ! tt->isEmpty(); tt = tt->next()) {
      if (tt->isVar()) {
	vars++;
	weight += 1;
      }
      else {
	Term* r = tt->term();
	vars += r->vars();
	weight += r->weight();
      }
    }
    t->markShared();
    t->setVars(vars);
    t->setWeight(weight);
    _totalLiterals++;
  }
  else {
    t->destroy();
  }
  return s;
} // TermSharing::insert

/**
 * True if the the top-levels of @b s and @b t are equal.
 * Used for inserting terms in a hash table.
 * @pre s and t must be non-variable terms
 * @since 28/12/2007 Manchester
 */
bool TermSharing::equals(const Term* s,const Term* t)
{
  CALL("TermSharing::equals");

  if (s->functor() != t->functor()) {
    return false;
  }
  const TermList* ss = s->args();
  const TermList* tt = t->args();
  while (! ss->isEmpty()) {
    if (ss->_content != tt->_content) {
      return false;
    }
    ss = ss->next();
    tt = tt->next();
  }
  return true;
} // TermSharing::equals
