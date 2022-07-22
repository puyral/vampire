/*
 * This file is part of the source code of the software program
 * Vampire. It is protected by applicable
 * copyright laws.
 *
 * This source code is distributed under the licence found here
 * https://vprover.github.io/license.html
 * and in the source directory
 */


#ifndef __TimeProfiling__
#define __TimeProfiling__

#include "Lib/Stack.hpp"
#include "Lib/Option.hpp"
#include "Kernel/Ordering.hpp"
#include "Debug/Assertion.hpp"
#include "Kernel/Term.hpp"
#include <chrono>
#include "Lib/MacroUtils.hpp"
#define ENABLE_TIME_PROFILING 1

namespace Shell {

#if ENABLE_TIME_PROFILING

#define TIME_TRACE(name)                                                                            \
  Shell::TimeTrace::ScopedTimer CONCAT_IDENTS(__time_trace_, __LINE__) (name);

#define TIME_TRACE_EXPR(name, ...)                                                                  \
  [&](){ TIME_TRACE(name); return __VA_ARGS__; }()

#define TIME_TRACE_NEW_ROOT(name)                                                                   \
  TIME_TRACE(name)                                                                                  \
  Shell::TimeTrace::ScopedChangeRoot CONCAT_IDENTS(__change_root_, __LINE__);

#else // !ENABLE_TIME_PROFILING

#define TIME_TRACE(name) {}
#define TIME_TRACE_EXPR(name, ...) __VA_ARGS__
#define TIME_TRACE_NEW_ROOT(name)

#endif // ENABLE_TIME_PROFILING


#if ENABLE_TIME_PROFILING
class TimeTrace 
{
  using Clock = std::chrono::high_resolution_clock;
  using Duration = typename Clock::duration;
  using TimePoint = typename Clock::time_point;

  TimeTrace(TimeTrace     &&) = delete;
  TimeTrace(TimeTrace const&) = delete;

  struct Node {
    CLASS_NAME(Node)
    USE_ALLOCATOR(Node)
    const char* name;
    Lib::Stack<unique_ptr<Node>> children;
    Lib::Stack<Duration> measurements;
    Node(const char* name) : name(name), children(), measurements() {}
    struct NodeFormatOpts ;
    void printPrettyRec(std::ostream& out, NodeFormatOpts& opts);
    void printPrettySelf(std::ostream& out, NodeFormatOpts& opts);
    void serialize(std::ostream& out);
    Duration totalDuration() const;

    Node flatten();
    struct FlattenState;
    void flatten_(FlattenState&);
  };

  friend std::ostream& operator<<(std::ostream& out, Duration const& self);
public:
  TimeTrace();

  class ScopedTimer {
    TimeTrace& _trace;
#if VDEBUG
    TimePoint _start;
    const char* _name;
#endif
  public:
    ScopedTimer(const char* name);
    ScopedTimer(TimeTrace& trace, const char* name);
    ~ScopedTimer();
  };


  class ScopedChangeRoot {
    TimeTrace& _trace;
  public:
    ScopedChangeRoot();
    ScopedChangeRoot(TimeTrace& trace);
    ~ScopedChangeRoot();
  };

  void printPretty(std::ostream& out);
  void serialize(std::ostream& out);
  void setEnabled(bool);

  struct Groups {
    static const char* PREPROCESSING;
    static const char* PARSING;
    static const char* LITERAL_ORDER_AFTERCHECK;
  };
private:

  Node _root;
  Lib::Stack<Node*> _tmpRoots;
  Lib::Stack<std::tuple<Node*, TimePoint>> _stack;
  bool _enabled;
};


template<class Ord>
class TimeTraceOrdering : public Kernel::Ordering
{
  const char* _nameLit;
  const char* _nameTerm;
  Ord _ord;
public:
  CLASS_NAME(TimeTraceOrdering);
  USE_ALLOCATOR(TimeTraceOrdering);

  TimeTraceOrdering(const char* nameLit, const char* nameTerm, Ord ord)
  : _nameLit(nameLit)
  , _nameTerm(nameTerm)
  , _ord(std::move(ord))
  { }

  ~TimeTraceOrdering() override {  }

  Result compare(Kernel::Literal* l1, Kernel::Literal* l2) const final override
  { TIME_TRACE(_nameLit); return _ord.compare(l1,l2); }

  Result compare(Kernel::TermList l1, Kernel::TermList l2) const final override
  { TIME_TRACE(_nameTerm); return _ord.compare(l1,l2); }

  // TODO shouldn't this be a function of PrecedenceOrdering?
  Kernel::Comparison compareFunctors(unsigned fun1, unsigned fun2) const final override 
  { ASSERTION_VIOLATION }

  void show(ostream& out) const final override
  { _ord.show(out); }

  Ord      & inner()       { return _ord; }
  Ord const& inner() const { return _ord; }
};
#endif // ENABLE_TIME_PROFILING

} // namespace Shell

#endif // __TimeProfiling__
