/*
 * This file is part of the source code of the software program
 * Vampire. It is protected by applicable
 * copyright laws.
 *
 * This source code is distributed under the licence found here
 * https://vprover.github.io/license.html
 * and in the source directory
 */
/**
 * @file InequalityResolutionIndex.hpp
 * Defines class InequalityResolutionIndex
 *
 */

#ifndef __IRC_InequalityResolutionIndex__
#define __IRC_InequalityResolutionIndex__


#include "Kernel/IRC.hpp"

#include "Indexing/IndexManager.hpp"
#include "Indexing/TermIndex.hpp"
#include "Indexing/Index.hpp"

namespace Indexing {

class InequalityResolutionIndex
: public TermIndex
{
public:
  CLASS_NAME(InequalityResolutionIndex);
  USE_ALLOCATOR(InequalityResolutionIndex);

  InequalityResolutionIndex(TermIndexingStructure* is)
    : TermIndex(is) {}

  void setShared(shared_ptr<Kernel::IrcState> shared) { _shared = std::move(shared); }
// protected:
  void handleClause(Clause* c, bool adding) final override;
private:
  template<class NumTraits> bool handleLiteral(Literal* lit, Clause* c, bool adding);

  shared_ptr<Kernel::IrcState> _shared;
};


class IRCSuperpositionRhsIndex
: public TermIndex
{
public:
  CLASS_NAME(IRCSuperpositionRhsIndex);
  USE_ALLOCATOR(IRCSuperpositionRhsIndex);

  IRCSuperpositionRhsIndex(TermIndexingStructure* is)
    : TermIndex(is) {}

  void setShared(shared_ptr<Kernel::IrcState> shared) { _shared = std::move(shared); }
// protected:
  void handleClause(Clause* c, bool adding) final override;
private:
  template<class NumTraits> bool handleInequality(Literal* lit, Clause* c, bool adding);
  bool handleUninterpreted(Literal* lit, Clause* c, bool adding);

  void handle(TermList t, Literal* lit, Clause* c, bool adding);

  shared_ptr<Kernel::IrcState> _shared;
};


class IRCSuperpositionLhsIndex
: public TermIndex
{
public:
  CLASS_NAME(IRCSuperpositionLhsIndex);
  USE_ALLOCATOR(IRCSuperpositionLhsIndex);

  IRCSuperpositionLhsIndex(TermIndexingStructure* is)
    : TermIndex(is) {}

  void setShared(shared_ptr<Kernel::IrcState> shared) { _shared = std::move(shared); }
// protected:
  void handleClause(Clause* c, bool adding) final override;
private:
  template<class NumTraits> bool handleInequality(Literal* lit, Clause* c, bool adding);

  void handle(TermList t, Literal* lit, Clause* c, bool adding);

  shared_ptr<Kernel::IrcState> _shared;
};

} // namespace Indexing

#endif // __IRC_InequalityResolutionIndex__