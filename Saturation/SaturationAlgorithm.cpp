/**
 * @file SaturationAlgorithm.cpp
 * Implementing SaturationAlgorithm class.
 */

#include "../Lib/Environment.hpp"
#include "../Lib/Metaiterators.hpp"
#include "../Lib/Stack.hpp"
#include "../Lib/Timer.hpp"
#include "../Lib/VirtualIterator.hpp"

#include "../Kernel/BDD.hpp"
#include "../Kernel/Clause.hpp"
#include "../Kernel/Inference.hpp"
#include "../Kernel/InferenceStore.hpp"
#include "../Kernel/LiteralSelector.hpp"
#include "../Kernel/MLVariant.hpp"

#include "../Shell/Options.hpp"
#include "../Shell/Statistics.hpp"

#include "SaturationAlgorithm.hpp"



using namespace Lib;
using namespace Kernel;
using namespace Shell;
using namespace Saturation;

/** Print information changes in clause containers */
#define REPORT_CONTAINERS 0
/** Print information about performed forward simplifications */
#define REPORT_FW_SIMPL 0
/** Print information about performed backward simplifications */
#define REPORT_BW_SIMPL 0
/** Perform simplification on a clause only if it leads to its deletion
 * (i.e. propositional part of the premise implies propositional part
 * of the simplified clause. */
#define TOTAL_SIMPLIFICATION_ONLY 1
/** Perform forward demodulation before a clause is passed to splitting */
#define FW_DEMODULATION_FIRST 1

/** Always split out propositional literals to the propositional part of the clause */
#define PROPOSITIONAL_PREDICATES_ALWAYS_TO_BDD 1

/**
 * Create a SaturationAlgorithm object
 *
 * The @b passiveContainer object will be used as a passive clause container, and
 * @b selector object to select literals before clauses are activated.
 */
SaturationAlgorithm::SaturationAlgorithm(PassiveClauseContainerSP passiveContainer,
	LiteralSelectorSP selector)
: _imgr(this), _passive(passiveContainer), _fwSimplifiers(0), _bwSimplifiers(0), _selector(selector)
{
  _performSplitting= env.options->splitting()!=Options::RA_OFF;

  _unprocessed=new UnprocessedClauseContainer();
  _active=new ActiveClauseContainer();

  _active->attach(this);
  _passive->attach(this);

  _active->addedEvent.subscribe(this, &SaturationAlgorithm::onActiveAdded);
  _active->removedEvent.subscribe(this, &SaturationAlgorithm::activeRemovedHandler);
  _passive->addedEvent.subscribe(this, &SaturationAlgorithm::onPassiveAdded);
  _passive->removedEvent.subscribe(this, &SaturationAlgorithm::passiveRemovedHandler);
  _passive->selectedEvent.subscribe(this, &SaturationAlgorithm::onPassiveSelected);
  _unprocessed->addedEvent.subscribe(this, &SaturationAlgorithm::onUnprocessedAdded);
  _unprocessed->removedEvent.subscribe(this, &SaturationAlgorithm::onUnprocessedRemoved);
  _unprocessed->selectedEvent.subscribe(this, &SaturationAlgorithm::onUnprocessedSelected);

  if(env.options->maxWeight()) {
    _limits.setLimits(-1,env.options->maxWeight());
  }

}

/**
 * Destroy the SaturationAlgorithm object
 */
SaturationAlgorithm::~SaturationAlgorithm()
{

  env.statistics->finalActiveClauses=_active->size();
  env.statistics->finalPassiveClauses=_passive->size();

  _active->detach();
  _passive->detach();

  if(_generator) {
    _generator->detach();
  }
  if(_immediateSimplifier) {
    _immediateSimplifier->detach();
  }

  _fwDemodulator->detach();
  while(_fwSimplifiers) {
    FwSimplList::pop(_fwSimplifiers)->detach();
  }
  while(_bwSimplifiers) {
    BwSimplList::pop(_bwSimplifiers)->detach();
  }

  delete _unprocessed;
  delete _active;
}

/**
 * A function that is called when a clause is added to the active clause container.
 */
void SaturationAlgorithm::onActiveAdded(Clause* c)
{
#if REPORT_CONTAINERS
  cout<<"## Active added: "<<(*c)<<endl;
#endif

  if(env.options->showActive()) {
    cout<<"Active: "<<c->toTPTPString()<<endl;
  }
}

/**
 * A function that is called when a clause is removed from the active clause container.
 */
void SaturationAlgorithm::onActiveRemoved(Clause* c)
{
  CALL("SaturationAlgorithm::onActiveRemoved");

#if REPORT_CONTAINERS
  cout<<"== Active removed: "<<(*c)<<endl;
#endif

  ASS(c->store()==Clause::ACTIVE || c->store()==Clause::REACTIVATED)

  if(c->store()==Clause::ACTIVE) {
    c->setStore(Clause::NONE);
  } else if(c->store()==Clause::REACTIVATED) {
    c->setStore(Clause::PASSIVE);
  }
#if VDEBUG
  else {
    ASSERTION_VIOLATION;
  }
#endif
}

/**
 * A function that is called when a clause is added to the passive clause container.
 */
void SaturationAlgorithm::onPassiveAdded(Clause* c)
{
#if REPORT_CONTAINERS
  cout<<"# Passive added: "<<(*c)<<endl;
#endif

  if(env.options->showPassive()) {
    cout<<"Passive: "<<c->toTPTPString()<<endl;
  }
  if(env.options->showNewPropositional() && c->isPropositional()) {
    VirtualIterator<string> clStrings=c->toSimpleClauseStrings();
    while(clStrings.hasNext()) {
      cout<<"New propositional: "<<clStrings.next()<<endl;
    }
  }
}

/**
 * A function that is called when a clause is removed from the active clause container.
 * The function is not called when a selected clause is removed from the passive container.
 * In this case the @b onPassiveSelected method is called.
 */
void SaturationAlgorithm::onPassiveRemoved(Clause* c)
{
  CALL("SaturationAlgorithm::onPassiveRemoved");

#if REPORT_CONTAINERS
  cout<<"= Passive removed: "<<(*c)<<endl;
#endif

  ASS(c->store()==Clause::PASSIVE || c->store()==Clause::REACTIVATED)

  if(c->store()==Clause::PASSIVE) {
    c->setStore(Clause::NONE);
  } else if(c->store()==Clause::REACTIVATED) {
    c->setStore(Clause::ACTIVE);
  }
#if VDEBUG
  else {
    ASSERTION_VIOLATION;
  }
#endif

}

/**
 * A function that is called when a clause is selected and removed from the passive
 * clause container to be activated.
 *
 * The clause @b c might not necessarily get to the activation, it can still be
 * removed by some simplification rule (in case of the Discount saturation algorithm).
 */
void SaturationAlgorithm::onPassiveSelected(Clause* c)
{
#if REPORT_CONTAINERS
  cout<<"~ Passive selected: "<<(*c)<<endl;
#endif
}

/**
 * A function that is called when a clause is added to the unprocessed clause container.
 */
void SaturationAlgorithm::onUnprocessedAdded(Clause* c)
{
#if REPORT_CONTAINERS
  cout<<"++ Unprocessed added: "<<(*c)<<endl;
#endif
}

/**
 * A function that is called when a clause is removed from the active clause container.
 */
void SaturationAlgorithm::onUnprocessedRemoved(Clause* c)
{
#if REPORT_CONTAINERS
  cout<<"-- Unprocessed removed: "<<(*c)<<endl;
#endif
}

void SaturationAlgorithm::onUnprocessedSelected(Clause* c)
{
#if REPORT_CONTAINERS
  cout<<"~~ Unprocessed selected: "<<(*c)<<endl;
#endif
}

/**
 * A function that is called whenever a possibly new clause appears.
 */
void SaturationAlgorithm::onNewClause(Clause* c)
{
  CALL("SaturationAlgorithm::onNewClause");

  if(env.options->showNew()) {
    cout<<"New: "<<c->toTPTPString()<<endl;
  }
}


/**
 * This function is subscribed to the remove event of the active container
 * instead of the @b onActiveRemoved function in the constructor, as the
 * @b onActiveRemoved function is virtual.
 */
void SaturationAlgorithm::activeRemovedHandler(Clause* cl)
{
  CALL("SaturationAlgorithm::activeRemovedHandler");

  onActiveRemoved(cl);
}

/**
 * This function is subscribed to the remove event of the passive container
 * instead of the @b onPassiveRemoved function in the constructor, as the
 * @b onPassiveRemoved function is virtual.
 */
void SaturationAlgorithm::passiveRemovedHandler(Clause* cl)
{
  CALL("SaturationAlgorithm::passiveRemovedHandler");

  onPassiveRemoved(cl);
}


/**
 * A function that is called as a first thing in the @b saturate() function.
 *
 * The @b saturate function is abstract and implemented by inheritors. It is therefore
 * up to them to call this function as they should.
 */
void SaturationAlgorithm::handleSaturationStart()
{
  _startTime=env.timer->elapsedMilliseconds();
}

int SaturationAlgorithm::elapsedTime()
{
  return env.timer->elapsedMilliseconds()-_startTime;
}


void SaturationAlgorithm::addInputClause(Clause* cl)
{
  ASS_EQ(cl->prop(),0);
  cl->setProp(BDD::instance()->getFalse());

#if PROPOSITIONAL_PREDICATES_ALWAYS_TO_BDD
  //put propositional predicates into BDD part
  cl=_propToBDD.simplify(cl);
#endif

  if(env.options->sos() && cl->inputType()==Clause::AXIOM) {
    addInputSOSClause(cl);
  } else {
    addUnprocessedClause(cl);
  }

  env.statistics->initialClauses++;
}

void SaturationAlgorithm::addInputSOSClause(Clause*& cl)
{
  onNewClause(cl);

  bool simplified;
  do {
    Clause* simplCl=_immediateSimplifier->simplify(cl);
    if(simplCl==0) {
      return;
    }
    simplified=simplCl!=cl;
    if(simplified) {
      ASS_EQ(simplCl->prop(), 0);
      simplCl->setProp(cl->prop());
      cl=simplCl;
      InferenceStore::instance()->recordNonPropInference(cl);

      onNewClause(cl);
    }
  } while(simplified);

  cl->setStore(Clause::ACTIVE);
  env.statistics->activeClauses++;
  _active->add(cl);
}


/**
 * Insert input clauses into ste unprocessed container.
 */
void SaturationAlgorithm::addInputClauses(ClauseIterator toAdd)
{

  while(toAdd.hasNext()) {
    Clause* cl=toAdd.next();
    addInputClause(cl);
  }

  if(env.options->splitting()==Options::RA_INPUT_ONLY) {
    _performSplitting=false;
  }
}

bool SaturationAlgorithm::isRefutation(Clause* c)
{
  CALL("SaturationAlgorithm::isRefutation");
  ASS(c->prop());

  BDD* bdd=BDD::instance();
  return c->isEmpty() && bdd->isFalse(c->prop());
}


class TotalSimplificationPerformer
: public ForwardSimplificationPerformer
{
public:
  TotalSimplificationPerformer(Clause* cl) : _cl(cl), _toAddLst(0) {}

  ~TotalSimplificationPerformer() { _toAddLst->destroy(); }

  void perform(Clause* premise, Clause* replacement)
  {
    CALL("TotalSimplificationPerformer::perform");
    ASS(_cl);
    ASS(willPerform(premise));

    BDD* bdd=BDD::instance();

    BDDNode* oldClProp=_cl->prop();

#if REPORT_FW_SIMPL
    cout<<"->>--------\n";
    if(premise) {
      cout<<":"<<(*premise)<<endl;
    }
    cout<<"-"<<(*_cl)<<endl;
#endif

    if(replacement) {
      replacement->setProp(oldClProp);
      InferenceStore::instance()->recordNonPropInference(replacement);
      ClauseList::push(replacement, _toAddLst);
    }

    _cl->setProp(bdd->getTrue());
    InferenceStore::instance()->recordPropReduce(_cl, oldClProp, bdd->getTrue());
    _cl=0;

#if REPORT_FW_SIMPL
    if(replacement) {
      cout<<"+"<<(*replacement)<<endl;
    }
    cout<<"removed\n";
    cout<<"^^^^^^^^^^^^\n";
#endif
  }

  bool willPerform(Clause* premise)
  {
    CALL("TotalSimplificationPerformer::willPerform");
    ASS(_cl);

    if(!premise) {
      return true;
    }

    BDD* bdd=BDD::instance();
    return bdd->isXOrNonYConstant(_cl->prop(), premise->prop(), true);
  }

  bool clauseKept()
  { return _cl; }

  ClauseIterator clausesToAdd()
  { return pvi( ClauseList::Iterator(_toAddLst) ); }
private:
  Clause* _cl;
  ClauseList* _toAddLst;
};

class PartialSimplificationPerformer
: public ForwardSimplificationPerformer
{
public:
  PartialSimplificationPerformer(Clause* cl) : _cl(cl), _toAddLst(0) {}

  ~PartialSimplificationPerformer() { _toAddLst->destroy(); }

  void perform(Clause* premise, Clause* replacement)
  {
    CALL("ForwardSimplificationPerformerImpl::perform");
    ASS(_cl);

    BDD* bdd=BDD::instance();

    BDDNode* oldClProp=_cl->prop();
    BDDNode* premiseProp = premise ? premise->prop() : bdd->getFalse();
    BDDNode* newClProp = bdd->xOrNonY(oldClProp, premiseProp);


#if REPORT_FW_SIMPL
    cout<<"->>--------\n";
    if(premise) {
      cout<<":"<<(*premise)<<endl;
    }
    cout<<"-"<<(*_cl)<<endl;
#endif


    if(replacement) {
      BDDNode* replacementProp=bdd->disjunction(oldClProp, premiseProp);
      if(!bdd->isTrue(replacementProp)) {
	replacement->setProp(replacementProp);
	InferenceStore::instance()->recordNonPropInference(replacement);
	ClauseList::push(replacement, _toAddLst);
      }
    }

    _cl->setProp(newClProp);
    InferenceStore::instance()->recordPropReduce(_cl, oldClProp, newClProp);

    if(bdd->isTrue(_cl->prop())) {
      _cl=0;
    }
#if REPORT_FW_SIMPL
    if(replacement) {
      cout<<"+"<<(*replacement)<<endl;
    }
    if(_cl) {
      cout<<">"<<(*_cl)<<endl;
      cout<<"^^^^^^^^^^^\n";
    } else {
      cout<<"removed\n";
      cout<<"^^^^^^^^^^^^\n";
    }
#endif
  }

  bool clauseKept()
  { return _cl; }

  ClauseIterator clausesToAdd()
  { return pvi( ClauseList::Iterator(_toAddLst) ); }
private:
  Clause* _cl;
  ClauseList* _toAddLst;
};

/**
 * Perform immediate simplifications on a clause and add it
 * to unprocessed.
 *
 * Splitting is also being performed in this method.
 */
void SaturationAlgorithm::addUnprocessedClause(Clause* cl)
{
  CALL("SaturationAlgorithm::addUnprocessedClause");
  ASS(cl->prop());

#if REPORT_CONTAINERS
  cout<<"$$ Unprocessed adding: "<<(*cl)<<endl;
#endif


  env.statistics->generatedClauses++;

  BDD* bdd=BDD::instance();
  ASS(!bdd->isTrue(cl->prop()));

  env.checkTimeSometime<64>();

simplificationStart:

  onNewClause(cl);

  BDDNode* prop=cl->prop();
  bool simplified;
  do {
    Clause* simplCl=_immediateSimplifier->simplify(cl);
    if(simplCl==0) {
      return;
    }
    simplified=simplCl!=cl;
    if(simplified) {
      ASS_EQ(simplCl->prop(), 0);
      cl=simplCl;
      cl->setProp(prop);
      InferenceStore::instance()->recordNonPropInference(cl);

      onNewClause(cl);
    }
  } while(simplified);


#if FW_DEMODULATION_FIRST
  if(_fwDemodulator) {
    TotalSimplificationPerformer demPerformer(cl);
    _fwDemodulator->perform(cl, &demPerformer);
    if(!demPerformer.clauseKept()) {
      ClauseIterator rit=demPerformer.clausesToAdd();
      if(!rit.hasNext()) {
	//clause was demodulated into a tautology
	return;
      }
      cl=rit.next();

      ASS(!rit.hasNext());
      goto simplificationStart;
    }
  }
#endif

  ASS(!bdd->isTrue(cl->prop()));

//  static int scCounter=0;
//  scCounter++;
//  if(scCounter==100) {
//    scCounter=0;
//    if(_performSplitting && elapsedTime()>(env.options->timeLimitInDeciseconds()*5)) {
//      _performSplitting=false;
//    }
//  }

  if(_performSplitting && !cl->isEmpty()) {
    ClauseIterator newComponents;
    ClauseIterator modifiedComponents;
    _splitter.doSplitting(cl, newComponents, modifiedComponents);
    while(newComponents.hasNext()) {
      Clause* comp=newComponents.next();
      ASS_EQ(comp->store(), Clause::NONE);
      ASS(!bdd->isTrue(comp->prop()));

      if(comp!=cl) {
	onNewClause(comp);
      }

      addUnprocessedFinalClause(comp);
    }
    while(modifiedComponents.hasNext()) {
      Clause* comp=modifiedComponents.next();

      ASS(!bdd->isTrue(comp->prop()));
      if(comp->store()==Clause::ACTIVE) {
        if(!comp->isEmpty()) { //don't reanimate empty clause
          reanimate(comp);
        } else {
          ASS(!isRefutation(comp));
        }
      } else if(comp->store()==Clause::NONE) {
        addUnprocessedFinalClause(comp);
      } else {
	ASS(comp->store()==Clause::PASSIVE ||
		comp->store()==Clause::REACTIVATED ||
		comp->store()==Clause::UNPROCESSED);
      }
      onNewClause(comp);
    }
  } else {
    addUnprocessedFinalClause(cl);
  }
}

void SaturationAlgorithm::addUnprocessedFinalClause(Clause* cl)
{
  CALL("SaturationAlgorithm::addUnprocessedFinalClause");

  BDD* bdd=BDD::instance();

  if( cl->isEmpty() && !bdd->isFalse(cl->prop()) ) {
    static BDDConjunction ecProp;
    static Stack<InferenceStore::ClauseSpec> emptyClauses;

    ecProp.addNode(cl->prop());
    if(ecProp.isFalse()) {
      InferenceStore::instance()->recordMerge(cl, cl->prop(), emptyClauses.begin(),
	      emptyClauses.size(), bdd->getFalse());
      cl->setProp(bdd->getFalse());
    } else {
      emptyClauses.push(InferenceStore::getClauseSpec(cl));
      return;
    }
  }

  cl->setStore(Clause::UNPROCESSED);
  _unprocessed->add(cl);
}


void SaturationAlgorithm::reanimate(Clause* cl)
{
  CALL("SaturationAlgorithm::reanimate");
  ASS_EQ(cl->store(), Clause::ACTIVE);

  ASS(!BDD::instance()->isTrue(cl->prop()));

  cl->setStore(Clause::REACTIVATED);
  _passive->add(cl);
}

bool SaturationAlgorithm::forwardSimplify(Clause* cl)
{
  CALL("SaturationAlgorithm::forwardSimplify");

  if(cl->store()==Clause::REACTIVATED) {
    return true;
  }

  if(!getLimits()->fulfillsLimits(cl)) {
    env.statistics->discardedNonRedundantClauses++;
    return false;
  }

#if TOTAL_SIMPLIFICATION_ONLY
  TotalSimplificationPerformer performer(cl);
#else
  PartialSimplificationPerformer performer(cl);
#endif

//#if FW_DEMODULATION_FIRST
//  FwSimplList::Iterator fsit(_fwSimplifiers);
//#else
  VirtualIterator<ForwardSimplificationEngineSP> fsit;
  if(_fwDemodulator) {
    fsit=pvi( getConcatenatedIterator(
	    FwSimplList::Iterator(_fwSimplifiers),
	    getSingletonIterator(_fwDemodulator)) );
  } else {
    fsit=pvi( FwSimplList::Iterator(_fwSimplifiers) );
  }
//#endif
  while(fsit.hasNext()) {
    ForwardSimplificationEngine* fse=fsit.next().ptr();

    fse->perform(cl, &performer);
    if(!performer.clauseKept()) {
      break;
    }
  }
  ClauseIterator replacements=performer.clausesToAdd();
  while(replacements.hasNext()) {
    addUnprocessedClause(replacements.next());
  }

  return performer.clauseKept();
}

void SaturationAlgorithm::backwardSimplify(Clause* cl)
{
  CALL("SaturationAlgorithm::backwardSimplify");

//  if(cl->store()==Clause::REACTIVATED) {
//    return;
//  }

  static Stack<Clause*> replacementsToAdd;

  BDD* bdd=BDD::instance();

  BwSimplList::Iterator bsit(_bwSimplifiers);
  while(bsit.hasNext()) {
    BackwardSimplificationEngine* bse=bsit.next().ptr();

    BwSimplificationRecordIterator simplifications;
    bse->perform(cl,simplifications);
    while(simplifications.hasNext()) {
      BwSimplificationRecord srec=simplifications.next();
      Clause* redundant=srec.toRemove;
      ASS_NEQ(redundant, cl);

      BDDNode* oldRedundantProp=redundant->prop();
      BDDNode* newRedundantProp;

#if TOTAL_SIMPLIFICATION_ONLY
      if( !bdd->isXOrNonYConstant(oldRedundantProp, cl->prop(), true) ) {
	continue;
      }
      newRedundantProp=bdd->getTrue();
#else
      newRedundantProp=bdd->xOrNonY(oldRedundantProp, cl->prop());
      if( newRedundantProp==oldRedundantProp ) {
	continue;
      }
#endif

#if REPORT_BW_SIMPL
      cout<<"-<<--------\n";
      cout<<":"<<(*cl)<<endl;
      cout<<"-"<<(*redundant)<<endl;

      if(MLVariant::isVariant(cl, redundant)) {
	cout<<"Variant!!!\n";
      }
#endif

      replacementsToAdd.reset();

      if(srec.replacements.hasNext()) {
	BDDNode* replacementProp=bdd->disjunction(oldRedundantProp, cl->prop());
	if(!bdd->isTrue(replacementProp)) {

	  while(srec.replacements.hasNext()) {
	    Clause* addCl=srec.replacements.next();
	    addCl->setProp(replacementProp);
	    InferenceStore::instance()->recordNonPropInference(addCl);
	    replacementsToAdd.push(addCl);
#if REPORT_BW_SIMPL
	    cout<<"+"<<(*addCl)<<endl;
#endif
	  }
	}
      }


      //we must remove the redundant clause before adding its replacement,
      //as otherwise the redundant one might demodulate the replacement into
      //a tautology

      redundant->setProp(newRedundantProp);
      InferenceStore::instance()->recordPropReduce(redundant, oldRedundantProp, newRedundantProp);

      if(bdd->isTrue(newRedundantProp)) {
	switch(redundant->store()) {
	case Clause::PASSIVE:
	  _passive->remove(redundant);
	  break;
	case Clause::ACTIVE:
	  _active->remove(redundant);
	  break;
	case Clause::REACTIVATED:
	  _passive->remove(redundant);
	  _active->remove(redundant);
	  break;
	default:
	  ASSERTION_VIOLATION;
	}
	redundant->setStore(Clause::NONE);
#if REPORT_BW_SIMPL
	cout<<"removed\n";
#endif
      }
//      else if(redundant->store()==Clause::ACTIVE) {
//	reanimate(redundant);
//      }

      unsigned addCnt=replacementsToAdd.size();
      for(unsigned i=0;i<addCnt;i++) {
	addUnprocessedClause(replacementsToAdd[i]);
      }
      replacementsToAdd.reset();


#if REPORT_BW_SIMPL
      cout<<"^^^^^^^^^^^\n";
#endif
    }
  }
}

void SaturationAlgorithm::addToPassive(Clause* c)
{
  CALL("SaturationAlgorithm::addToPassive");

  ASS_EQ(c->store(), Clause::UNPROCESSED);
  c->setStore(Clause::PASSIVE);
  env.statistics->passiveClauses++;

  _passive->add(c);
}

void SaturationAlgorithm::activate(Clause* cl)
{
  CALL("SaturationAlgorithm::activate");

  if(!cl->selected()) {
    _selector->select(cl);
  }

  if(cl->store()==Clause::REACTIVATED) {
    cl->setStore(Clause::ACTIVE);
#if REPORT_CONTAINERS
    cout<<"** Reanimated: "<<(*cl)<<endl;
#endif
  } else {
    ASS_EQ(cl->store(), Clause::PASSIVE);
    cl->setStore(Clause::ACTIVE);
    env.statistics->activeClauses++;

    _active->add(cl);
  }

  ClauseIterator toAdd=_generator->generateClauses(cl);

  BDD* bdd=BDD::instance();
  while(toAdd.hasNext()) {
    Clause* genCl=toAdd.next();

    BDDNode* prop=bdd->getFalse();
    Inference::Iterator iit=genCl->inference()->iterator();
    while(genCl->inference()->hasNext(iit)) {
      Unit* premUnit=genCl->inference()->next(iit);
      ASS(premUnit->isClause());
      Clause* premCl=static_cast<Clause*>(premUnit);
//      cout<<"premCl: "<<(*premCl)<<endl;
      prop=bdd->disjunction(prop, premCl->prop());
//      cout<<"res: "<<bdd->toString(prop)<<endl;
    }


    genCl->setProp(prop);
#if REPORT_CONTAINERS
    cout<<"G "<<(*genCl)<<endl;
#endif

    if(bdd->isTrue(prop)) {
      continue;
    }

    InferenceStore::instance()->recordNonPropInference(genCl);

    addUnprocessedClause(genCl);
  }
}

void SaturationAlgorithm::setGeneratingInferenceEngine(GeneratingInferenceEngineSP generator)
{
  ASS(!_generator);
  _generator=generator;
  _generator->attach(this);
}
void SaturationAlgorithm::setImmediateSimplificationEngine(ImmediateSimplificationEngineSP immediateSimplifier)
{
  ASS(!_immediateSimplifier);
  _immediateSimplifier=immediateSimplifier;
  _immediateSimplifier->attach(this);
}

void SaturationAlgorithm::setFwDemodulator(ForwardSimplificationEngineSP fwDemodulator)
{
  _fwDemodulator=fwDemodulator;
  fwDemodulator->attach(this);
}

void SaturationAlgorithm::addForwardSimplifierToFront(ForwardSimplificationEngineSP fwSimplifier)
{
  FwSimplList::push(fwSimplifier, _fwSimplifiers);
  fwSimplifier->attach(this);
}

void SaturationAlgorithm::addBackwardSimplifierToFront(BackwardSimplificationEngineSP bwSimplifier)
{
  BwSimplList::push(bwSimplifier, _bwSimplifiers);
  bwSimplifier->attach(this);
}
