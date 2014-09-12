/**
 * @file Environment.cpp
 * Implements environment used by the current prover.
 *
 * @since 06/05/2007 Manchester
 */

#include "Debug/Tracer.hpp"

#include "Lib/Sys/SyncPipe.hpp"

#include "Indexing/TermSharing.hpp"

#include "Kernel/MainLoopContext.hpp"
#include "Kernel/Signature.hpp"
#include "Kernel/Sorts.hpp"

#include "Shell/Options.hpp"
#include "Shell/Statistics.hpp"

#include "Timer.hpp"

#include "Environment.hpp"

namespace Lib
{

using namespace std;
using namespace Kernel;
using namespace Indexing;
using namespace Shell;

#if COMPIT_GENERATOR
struct nullstream:
public std::ostream {
  struct nullbuf: public std::streambuf {
    int overflow(int c) { return traits_type::not_eof(c); }
  } m_sbuf;
  nullstream(): std::ios(&m_sbuf), std::ostream(&m_sbuf) {}
};
nullstream nullStream;
#endif

/**
 * @since 06/05/2007 Manchester
 */
Environment::Environment()
  : options(0),
	signature(0),
    sharing(0),
    property(0),
    ordering(0),
    colorUsed(false),
    _outputDepth(0),
    _priorityOutput(0),
    _pipe(0)
{
  // Will be set in Shell::CommandLine::interpret
  //options = new Options();//TODO This object is needed only because of global iostream
  //optionsList = 0;
  statistics = new Statistics;
  timer = new Timer;
  sorts = new Sorts;
  signature = new Signature;
  sharing = new TermSharing;

  timer->start();
} // Environment::Environment

Environment::Environment(const Environment& e, Options& opts)
	  : options(&opts),
	    signature(0),
	    sharing(0),
	    property(0),
	    ordering(0),
	    colorUsed(false),
	    _outputDepth(0),
	    _priorityOutput(0),
	    _pipe(0)
	{
	  optionsList = e.optionsList;
	  statistics = new Statistics;
	  timer = e.timer;
	  sorts = e.sorts;
	  signature = e.signature;
	  sharing = e.sharing;

	  //timer->start();//Timer is already started in e
	} // Environment::Environment

Environment::~Environment()
{
  CALL("Environment::~Environment");

  //in the usual cases the _outputDepth should be zero at this point, but in case of
  //thrown exceptions this might not be true.
//  ASS_EQ(_outputDepth,0);

  while(_outputDepth!=0) {
    endOutput();
  }

#if CHECK_LEAKS
  cout << "running this code" << endl;
  delete sharing;
  if(signature){delete signature; signature = 0;}
  delete sorts;
  if(timer){ delete timer; timer = 0;}
  delete statistics;
  delete options;
#endif



}

/**
 * If the global time limit reached set Statistics::terminationReason
 * to TIME_LIMIT and return true, otherwise return false.
 * @since 25/03/2008 Torrevieja
 */
bool Environment::timeLimitReached() const
{
  CALL("Environment::timeLimitReached");

  if (options->timeLimitInDeciseconds() &&
      timer->elapsedDeciseconds() > options->timeLimitInDeciseconds()) {
    statistics->terminationReason = Shell::Statistics::TIME_LIMIT;
    return true;
  }
  return false;
} // Environment::timeLimitReached

void Environment::checkAllTimeLimits() const
{
  CALL("Environment::checkAllTimeLimits");

  if (options->timeLimitInDeciseconds() &&
      timer->elapsedDeciseconds() > options->timeLimitInDeciseconds()) {
    statistics->terminationReason = Shell::Statistics::TIME_LIMIT;
    cout << "throwing time limit exception" << endl;
    throw TimeLimitExceededException();
  }else if (MainLoopContext::currentContext && options->localTimeLimitInDeciseconds() &&
	      MainLoopContext::currentContext -> updateTimeCounter() > options -> localTimeLimitInDeciseconds()) {
	    statistics->terminationReason = Shell::Statistics::LOCAL_TIME_LIMIT;
	    	ASS(MainLoopContext::currentContext -> checkEnvironment(this));
    		cout << "throwing local time limit exception " <<
    				MainLoopContext::currentContext -> elapsedDeciseconds() <<
    				"exceeds "<< options -> localTimeLimitInDeciseconds() << "dsec"
    				<< endl;
	    throw LocalTimeLimitExceededException();
  }
} // Environment::timeLimitReached

/**
 * Return remaining time in miliseconds.
 */
int Environment::remainingTime() const
{
  return options->timeLimitInDeciseconds()*100 - timer->elapsedMilliseconds();
}

/**
 * Acquire an output stream
 *
 * A process cannot hold an output stream during forking.
 */
void Environment::beginOutput()
{
  CALL("Environment::beginOutput");
  ASS_GE(_outputDepth,0);

  _outputDepth++;
  if(_outputDepth==1 && _pipe) {
    _pipe->acquireWrite();
  }
}

/**
 * Release the output stream
 */
void Environment::endOutput()
{
  CALL("Environment::endOutput");
  ASS_G(_outputDepth,0);

  _outputDepth--;
  if(_outputDepth==0) {
    if(_pipe) {
      cout.flush();
      _pipe->releaseWrite();
    }
    else {
      cout.flush();
    }
  }
}

/**
 * Return true if we have an output stream acquired
 */
bool Environment::haveOutput()
{
  CALL("Environment::haveOutput");

  return _outputDepth;
}

/**
 * Return the output stream if we have it acquired
 *
 * Process must have an output stream acquired in order to call
 * this function.
 */
ostream& Environment::out()
{
  CALL("Environment::out");
  ASS(_outputDepth);

#if COMPIT_GENERATOR
  static nullstream ns;
  return ns;
#else
  if(_priorityOutput) {
    return *_priorityOutput;
  }
  else if(_pipe) {
    return _pipe->out();
  }
  else {
    return cout;
  }
#endif
}

/**
 * Direct @b env -> out() into @b pipe or to @b cout if @b pipe is zero
 *
 * This function cannot be called when an output is in progress.
 */
void Environment::setPipeOutput(SyncPipe* pipe)
{
  CALL("Environment::setPipeOutput");
  ASS(!haveOutput());

  _pipe=pipe;
}

void Environment::setPriorityOutput(ostream* stm)
{
  CALL("Environment::setPriorityOutput");
  ASS(!_priorityOutput || !stm);

  _priorityOutput=stm;

}

}
