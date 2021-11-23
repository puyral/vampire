/*
 * This file is part of the source code of the software program
 * Vampire. It is protected by applicable
 * copyright laws.
 *
 * This source code is distributed under the licence found here
 * https://vprover.github.io/license.html
 * and in the source directory
 */

#include "Test/UnitTesting.hpp"
#include "Test/SyntaxSugar.hpp"
#include "Indexing/TermSharing.hpp"
#include "Inferences/IRC/InequalityResolution.hpp"
#include "Inferences/InterpretedEvaluation.hpp"
#include "Kernel/Ordering.hpp"
#include "Inferences/PolynomialEvaluation.hpp"
#include "Inferences/Cancellation.hpp"

#include "Test/SyntaxSugar.hpp"
#include "Test/TestUtils.hpp"
#include "Lib/Coproduct.hpp"
#include "Test/SimplificationTester.hpp"
#include "Test/GenerationTester.hpp"
#include "Kernel/KBO.hpp"
#include "Indexing/TermSubstitutionTree.hpp"
#include "Inferences/PolynomialEvaluation.hpp"

using namespace std;
using namespace Kernel;
using namespace Inferences;
using namespace Test;
using namespace Indexing;
using namespace Inferences::IRC;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////// TEST CASES 
/////////////////////////////////////

#define SUGAR(Num)                                                                                            \
  NUMBER_SUGAR(Num)                                                                                           \
  DECL_DEFAULT_VARS                                                                                           \
  DECL_VAR(x0, 0)                                                                                             \
  DECL_VAR(x1, 1)                                                                                             \
  DECL_VAR(x2, 2)                                                                                             \
  DECL_VAR(x3, 3)                                                                                             \
  DECL_VAR(x4, 4)                                                                                             \
  DECL_VAR(x5, 5)                                                                                             \
  DECL_VAR(x6, 6)                                                                                             \
  DECL_VAR(x7, 7)                                                                                             \
  DECL_VAR(x8, 8)                                                                                             \
  DECL_VAR(x9, 9)                                                                                             \
  DECL_VAR(x10, 10)                                                                                             \
  DECL_FUNC(f, {Num}, Num)                                                                                    \
  DECL_FUNC(g, {Num, Num}, Num)                                                                               \
  DECL_CONST(a, Num)                                                                                          \
  DECL_CONST(a0, Num)                                                                                         \
  DECL_CONST(a1, Num)                                                                                         \
  DECL_CONST(a2, Num)                                                                                         \
  DECL_CONST(a3, Num)                                                                                         \
  DECL_CONST(b, Num)                                                                                          \
  DECL_CONST(c, Num)                                                                                          \
  DECL_PRED(r, {Num,Num})                                                                                     \

#define MY_SYNTAX_SUGAR SUGAR(Rat)

#define UWA_MODE Options::UnificationWithAbstraction::IRC1

std::function<Indexing::Index*()> inequalityResolutionIdx(
   Options::UnificationWithAbstraction uwa = Options::UnificationWithAbstraction::IRC1
    ) 
{ return [=]() {return new InequalityResolutionIndex(new TermSubstitutionTree(uwa, true)); }; }

InequalityResolution testInequalityResolution(
   Options::UnificationWithAbstraction uwa = Options::UnificationWithAbstraction::IRC1
    ) 
{ return InequalityResolution(testIrcState(uwa)); }

REGISTER_GEN_TESTER(Test::Generation::GenerationTester<InequalityResolution>(testInequalityResolution()))

/////////////////////////////////////////////////////////
// Basic tests
//////////////////////////////////////

TEST_GENERATION(basic01,
    Generation::SymmetricTest()
      .indices({ inequalityResolutionIdx() })
      .inputs  ({ clause({selected( f(x) > 0 ), x == 7   }) 
               ,  clause({selected(-f(x) > 0 )           }) })
      .expected(exactly(
            clause({ num(0) > 0,  x == 7  })
      ))
      .premiseRedundant(false)
    )

TEST_GENERATION(basic02,
    Generation::SymmetricTest()
      .indices({ inequalityResolutionIdx() })
      .inputs  ({ clause({selected(     f(a) > 0) }) 
               ,  clause({selected(a + -f(a) > 0) }) })
      .expected(exactly(
            clause({  a > 0  })
      ))
      .premiseRedundant(false)
    )

TEST_GENERATION(basic03,
    Generation::SymmetricTest()
      .indices({ inequalityResolutionIdx() })
      .inputs  ({ clause({selected( -g(x,a) + -g(g(a,b), f(x)) > 0) }) 
               ,  clause({selected(  g(b,a) +  g(g(a,b), f(a)) > 0) }) })
      .expected(exactly(
            clause({  g(b,a) + (-g(a,a))  > 0  })
      ))
      .premiseRedundant(false)
    )

TEST_GENERATION(basic04,
    Generation::SymmetricTest()
      .indices({ inequalityResolutionIdx() })
      .inputs  ({ clause({ selected( a + -f(x) > 0), x == 7 })  
               ,  clause({ selected( a +  f(a) > 0) })         })
      .expected(exactly(
            clause({  a +  a > 0, a == 7  })
      ))
      .premiseRedundant(false)
    )

TEST_GENERATION(basic04_variation,
    Generation::SymmetricTest()
      .indices({ inequalityResolutionIdx() })
      .inputs  ({ clause({ selected( a +  f(a) > 0) })          
               ,  clause({ selected( a + -f(x) > 0), x == 7 }) })
      .expected(exactly(
            clause({  a + a > 0, a == 7  })
      ))
      .premiseRedundant(false)
    )

TEST_GENERATION(basic05,
    Generation::SymmetricTest()
      .indices({ inequalityResolutionIdx() })
      .inputs  ({ clause({ selected( a + -f(y) > 0) }) 
               ,  clause({ selected( a +  f(a) > 0), x == 7}) })
      .expected(exactly(
            clause({  a + a > 0, x == 7  })
      ))
      .premiseRedundant(false)
    )

TEST_GENERATION(basic06,
    Generation::SymmetricTest()
      .indices({ inequalityResolutionIdx() })
      .inputs  ({ clause({ selected(         -g(x,y) > 0 ) }) 
               ,  clause({ selected( g(a,z) + g(z,a) > 0 ) }) })
      .expected(exactly(
            clause({  g(x,a) > 0 })
          , clause({  g(a,y) > 0 })
      ))
      .premiseRedundant(false)
    )

TEST_GENERATION(basic07,
    Generation::SymmetricTest()
      .indices({ inequalityResolutionIdx() })
      .inputs  ({ clause({ selected(a > 0) }) 
               ,  clause({ selected(a > 0) }) })
      .expected(exactly(
      ))
      .premiseRedundant(false)
    )

TEST_GENERATION(basic08,
    Generation::SymmetricTest()
      .indices({ inequalityResolutionIdx() })
      .inputs  ({ clause({ selected(a >= a) }) 
               ,  clause({ selected(a > 0 )}) })
      .expected(exactly(
      ))
      .premiseRedundant(false)
    )

TEST_GENERATION(basic09,
    Generation::SymmetricTest()
      .indices({ inequalityResolutionIdx() })
      .inputs  ({ clause({ selected(-a > 0) }) 
               ,  clause({           a > 0  }) })
      .expected(exactly(
          clause({ num(0) > 0 })
      ))
      .premiseRedundant(false)
    )

TEST_GENERATION(greater_equal01a,
    Generation::SymmetricTest()
      .indices({ inequalityResolutionIdx() })
      .inputs  ({ clause({ selected( a + -f(y) >= 0) }) 
               ,  clause({ selected( a +  f(a) >= 0), x == 7}) })
      .expected(exactly(
            clause({  a + a > 0, f(a) + a == 0, x == 7  })
      ))
      .premiseRedundant(false)
    )

TEST_GENERATION(greater_equal01b,
    Generation::SymmetricTest()
      .indices({ inequalityResolutionIdx() })
      .inputs  ({ clause({ selected( a + -f(y) >= 0) }) 
               ,  clause({ selected( a +  f(a) >  0), x == 7}) })
      .expected(exactly(
            clause({  a + a > 0, x == 7  })
      ))
      .premiseRedundant(false)
    )

TEST_GENERATION(greater_equal01c,
    Generation::SymmetricTest()
      .indices({ inequalityResolutionIdx() })
      .inputs  ({ clause({ selected( a + -f(y) >  0) }) 
               ,  clause({ selected( a +  f(a) >= 0), x == 7}) })
      .expected(exactly(
            clause({  a + a > 0, x == 7  })
      ))
      .premiseRedundant(false)
    )


TEST_GENERATION(strictly_max_after_unification_01a,
    Generation::SymmetricTest()
      .indices({ inequalityResolutionIdx() })
      .inputs  ({ clause({ selected(-f(x) + f(a) > 0) }) 
               ,  clause({ selected( f(a)        > 0) }) })
      .expected(exactly(
      ))
      .premiseRedundant(false)
    )


TEST_GENERATION(strictly_max_after_unification_01b,
    Generation::SymmetricTest()
      .indices({ inequalityResolutionIdx() })
      .inputs  ({ clause({ selected( f(a)        > 0) })  
               ,  clause({ selected(-f(x) + f(a) > 0) }) })
      .expected(exactly(
      ))
      .premiseRedundant(false)
    )

TEST_GENERATION(strictly_max_after_unification_02a,
    Generation::SymmetricTest()
      .indices({ inequalityResolutionIdx() })
      .inputs  ({        clause({ selected(-f(x) + f(a) > 0 )}) 
               ,         clause({ selected( f(b)        > 0) }) })
      .expected(exactly( clause({           f(a)        > 0 }) ))
      .premiseRedundant(false)
    )

TEST_GENERATION(strictly_max_after_unification_02b,
    Generation::SymmetricTest()
      .indices({ inequalityResolutionIdx() })
      .inputs  ({        clause({ selected( f(b)        > 0) })  
               ,         clause({ selected(-f(x) + f(a) > 0 )}) })
      .expected(exactly( clause({           f(a)        > 0 }) ))
      .premiseRedundant(false)
    )

/////////////////////////////////////////////////////////
// Testing substitution application
//////////////////////////////////////

TEST_GENERATION(substitution01,
    Generation::SymmetricTest()
      .indices({ inequalityResolutionIdx() })
      .inputs  ({ clause({ selected(-f(f(x)) + f(x) > 0) })  
               ,  clause({ selected( f(f(a))        > 0) }) })
      .expected(exactly(
            clause({  f(a) > 0 })
      ))
      .premiseRedundant(false)
    )

TEST_GENERATION(substitution02,
    Generation::SymmetricTest()
      .indices({ inequalityResolutionIdx() })
      .inputs  ({ clause({ selected(-g(f(x), f(f(b))) +    f(x)  > 0) })  
               ,  clause({ selected( g(f(a), f(f(y))) +    f(y)  > 0) }) })
      .expected(exactly(
            clause({  f(a) + f(b) > 0 })
      ))
      .premiseRedundant(false)
    )

/////////////////////////////////////////////////////////
// Testing abstraction
//////////////////////////////////////

TEST_GENERATION(abstraction1,
    Generation::SymmetricTest()
      .indices({ inequalityResolutionIdx() })
      .inputs  ({ clause({ selected(-f(     0        ) > 0) })  
               ,  clause({ selected( f(f(a) + g(b, c)) > 0) }) })
      .expected(exactly(
            clause({ num(0) > 0, f(a) + g(b, c) != 0 })
      ))
      .premiseRedundant(false)
    )

TEST_GENERATION(abstraction2,
    Generation::SymmetricTest()
      .rule(    new InequalityResolution(testInequalityResolution(Options::UnificationWithAbstraction::IRC1))  )
      .indices({ inequalityResolutionIdx(Options::UnificationWithAbstraction::IRC1) })
      .inputs  ({         clause({ selected(-f(     0        ) > 0) })  
               ,          clause({ selected( f(f(a) + g(b, c)) > 0) }) })
      .expected(exactly(  clause({ num(0) > 0, f(a) + g(b, c) != 0  }) ))
      .premiseRedundant(false)
    )

TEST_GENERATION(abstraction3,
    Generation::SymmetricTest()
      .rule(    new InequalityResolution(testInequalityResolution(Options::UnificationWithAbstraction::IRC1))  )
      .indices({ inequalityResolutionIdx(Options::UnificationWithAbstraction::IRC1) })
      .inputs  ({ clause({ selected(-f(b) > 0) })  
               ,  clause({ selected( f(a) > 0) }) })
      .expected(exactly())
      .premiseRedundant(false)
    )

TEST_GENERATION(abstraction4,
    Generation::SymmetricTest()
      .rule(    new InequalityResolution(testInequalityResolution(Options::UnificationWithAbstraction::IRC1))  )
      .indices({ inequalityResolutionIdx(Options::UnificationWithAbstraction::IRC1) })
      .inputs  ({        clause({ -f(3 * a) > 0 })  
               ,         clause({  f(7 * a) > 0 }) })
      .expected(exactly(                           ))
      .premiseRedundant(false)
    )

TEST_GENERATION(abstraction5,
    Generation::SymmetricTest()
      .rule(    new InequalityResolution(testInequalityResolution(Options::UnificationWithAbstraction::IRC1))  )
      .indices({ inequalityResolutionIdx(Options::UnificationWithAbstraction::IRC1) })
      .inputs  ({        clause({ -f(a + b) > 0 })  
               ,         clause({  f(7 * a) > 0 }) })
      .expected(exactly( clause({ num(0) > 0, a + b != 7 * a }) ))
      .premiseRedundant(false)
    )

TEST_GENERATION(abstraction6,
    Generation::SymmetricTest()
      .rule(    new InequalityResolution(testInequalityResolution(Options::UnificationWithAbstraction::IRC1))  )
      .indices({ inequalityResolutionIdx(Options::UnificationWithAbstraction::IRC1) })
      .inputs  ({        clause({ -f(g(a,x)) > 0 })  
               ,         clause({  f(7 * y)  > 0 }) })
      .expected(exactly( clause({ num(0) > 0, g(a,x) != 7 * y }) ))
      .premiseRedundant(false)
    )

TEST_GENERATION(abstraction7,
    Generation::SymmetricTest()
      .rule(    new InequalityResolution(testInequalityResolution(Options::UnificationWithAbstraction::IRC1))  )
      .indices({ inequalityResolutionIdx(Options::UnificationWithAbstraction::IRC1) })
      .inputs  ({        clause({ -f(a + b) > 0 })           
               ,         clause({  f(c) > 0 })              })
      .expected(exactly( clause({ num(0) > 0, c != a + b }) ))
      .premiseRedundant(false)
    )

TEST_GENERATION(abstraction1_irc2,
    Generation::SymmetricTest()
      .rule(    new InequalityResolution(testInequalityResolution(Options::UnificationWithAbstraction::IRC2))  )
      .indices({ inequalityResolutionIdx(Options::UnificationWithAbstraction::IRC2) })
      .inputs  ({        clause({ -f(a + b) > 0 })           
               ,         clause({  f(c) > 0 })              })
      .expected(exactly(                                    ))
      .premiseRedundant(false)
    )

TEST_GENERATION(abstraction2_irc2,
    Generation::SymmetricTest()
      .rule(    new InequalityResolution(testInequalityResolution(Options::UnificationWithAbstraction::IRC2))  )
      .indices({ inequalityResolutionIdx(Options::UnificationWithAbstraction::IRC2) })
      .inputs  ({        clause({ -f(a + b) > 0 })           
               ,         clause({  f(c + x) > 0 })              })
      .expected(exactly( clause({  num(0) > 0, c + x != a + b   }) ))
      .premiseRedundant(false)
    )

TEST_GENERATION(abstraction3_irc2,
    Generation::SymmetricTest()
      .rule(    new InequalityResolution(testInequalityResolution(Options::UnificationWithAbstraction::IRC2))  )
      .indices({ inequalityResolutionIdx(Options::UnificationWithAbstraction::IRC2) })
      .inputs  ({        clause({ -f(3 * a) > 0 })           
               ,         clause({  f(4 * a) > 0 })              })
      .expected(exactly(   ))
      .premiseRedundant(false)
    )

TEST_GENERATION(abstraction4_irc2,
    Generation::SymmetricTest()
      .rule(    new InequalityResolution(testInequalityResolution(Options::UnificationWithAbstraction::IRC2))  )
      .indices({ inequalityResolutionIdx(Options::UnificationWithAbstraction::IRC2) })
      .inputs  ({        clause({ -f(-a ) > 0 })           
               ,         clause({  f( a ) > 0 })              })
      .expected(exactly(   ))
      .premiseRedundant(false)
    )

TEST_GENERATION(abstraction5_irc2,
    Generation::SymmetricTest()
      .rule(    new InequalityResolution(testInequalityResolution(Options::UnificationWithAbstraction::IRC2))  )
      .indices({ inequalityResolutionIdx(Options::UnificationWithAbstraction::IRC2) })
      .inputs  ({        clause({ -f( a ) > 0 })           
               ,         clause({  f( a + f(b) ) > 0 })              })
      .expected(exactly(   ))
      .premiseRedundant(false)
    )



/////////////////////////////////////////////////////////
// Testing normalization
//////////////////////////////////////

TEST_GENERATION(normalization01,
    Generation::SymmetricTest()
      .indices({ inequalityResolutionIdx() })
      .inputs  ({ clause({ selected(0 > f(a)    ) }) 
               ,  clause({ selected(a + f(a) > 0) }) })
      .expected(exactly(
            clause({  a > 0  })
      ))
      .premiseRedundant(false)
    )

TEST_GENERATION_WITH_SUGAR(normalization02_Int,
    SUGAR(Int),
    Generation::SymmetricTest()
      .indices({ inequalityResolutionIdx() })
      .inputs  ({ clause({selected(~(0 > -f(a)))  }) 
      //                           ~(0 > -f(a)))
      //                     ==> -f(a) >= 0         ==> 0 >=  f(a)
      //                     ==> -f(a) + 1 > 0      
               ,  clause({selected(a + f(a) > 0) }) }) // ==> a + f(a) >  f(a)
                                                       // ==> a        > 0
      .expected(exactly(
            clause({ -1 + a + 1 > 0  })
      ))
      .premiseRedundant(false)
    )

TEST_GENERATION_WITH_SUGAR(normalization02_Rat,
    SUGAR(Rat),
    Generation::SymmetricTest()
      .indices({ inequalityResolutionIdx() })
      .inputs  ({ clause({selected(~(0 > -f(a)))  }) 
               ,  clause({selected( a + f(a) > 0) }) })
      .expected(exactly(
            clause({  a > 0  })
      ))
      .premiseRedundant(false)
    )

// only for integers (which we r using here)
TEST_GENERATION_WITH_SUGAR(normalization03,
    SUGAR(Int),
    Generation::SymmetricTest()
      .indices({ inequalityResolutionIdx() })
      .inputs  ({ clause({selected(     f(a) >= 0) }) 
               ,  clause({selected(a + -f(a) >  0) }) })
      .expected(exactly(
            clause({ -1 +  a + 1 > 0  })
      ))
      .premiseRedundant(false)
    )

TEST_GENERATION_WITH_SUGAR(bug01a,
    SUGAR(Real),
    Generation::SymmetricTest()
      .indices({ inequalityResolutionIdx() })
      .inputs  ({ clause({ r(x, y), selected( (f(x) + -f(y) > 0) ) }) 
               ,  clause({ selected(f(a) >  0) }) })
      //                                      (y - 1 > 0) 
      .expected(exactly(
      ))
      .premiseRedundant(false)
    )

TEST_GENERATION_WITH_SUGAR(bug02,
    SUGAR(Real),
    Generation::SymmetricTest()
      .indices({ inequalityResolutionIdx() })
      .inputs  ({        clause({ selected( 3 +  a  > 0 ) })  
               ,         clause({ selected( 0 + -a  > 0 ) }) })
      .expected(exactly( clause({            num(3) > 0   }) ))
      .premiseRedundant(false)
    )

TEST_GENERATION_WITH_SUGAR(bug03a,
    SUGAR(Real),
    Generation::SymmetricTest()
// *cl2 = ~P(X1,X2) | 1 + -X1 + a > 0
// *resolvent = $greater($sum(1,$uminus(X1)),0) | ~'MS'(X0,X1,s2)
      .indices({ inequalityResolutionIdx() })
      .inputs  ({        clause({            selected(1 + -f(a)        > 0) })  
               ,         clause({  ~r(y,z) , selected(1 + -f(x) + f(a) > 0) }) })
      .expected(exactly(                      ))
      .premiseRedundant(false)
    )

TEST_GENERATION_WITH_SUGAR(bug03b,
    SUGAR(Real),
    Generation::SymmetricTest()
// *cl2 = ~P(X1,X2) | 1 + -X1 + a > 0
// *resolvent = $greater($sum(1,$uminus(X1)),0) | ~'MS'(X0,X1,s2)
      .indices({ inequalityResolutionIdx() })
      .inputs  ({        clause({           selected(1 + -f(a)            > 0) })  
               ,         clause({  a != 1 , selected(1 + -f(x) + f(a)     > 0) }) })
      .expected(exactly( clause({  a != 1 ,          1 + -f(x)        + 1 > 0  }) ))
      .premiseRedundant(false)
    )


TEST_GENERATION_WITH_SUGAR(bug_overflow_01,
    SUGAR(Real),
    Generation::SymmetricTest()
      .indices({ inequalityResolutionIdx() })
      .inputs  ({ clause({ selected(          num(2) * (1073741824 * a + 536870912) > 0 ) })  
               ,  clause({ selected(num(-1) * num(2) * (1073741824 * a + 536870912) > 0 )   }) })
      .expected(exactly(
          // clause({ num(0) > 0 }) // we don't perform the rule if we overflow
      ))
      .premiseRedundant(false)
    )

  // 2 f13(f14, 1) 1073741824

TEST_GENERATION_WITH_SUGAR(bug_overflow_02,
    SUGAR(Int),
    Generation::SymmetricTest()
      .indices({ inequalityResolutionIdx() })
      .inputs  ({ clause({ selected( 0 < 2 * (f(a) * num(1073741824)) ) })  
               ,  clause({ selected( 3  + -a > 0 )  }) })
      .expected(exactly(
      ))
      .premiseRedundant(false)
    )


TEST_GENERATION_WITH_SUGAR(misc01,
    SUGAR(Real),
    Generation::SymmetricTest()
      .indices({ inequalityResolutionIdx() })
      .inputs  ({        clause({ -f(x0 + -x1 + g(x0,x1)) > 0 })
               ,         clause({  f(x2 + -g(x3,x2)               ) > 0 }) })
      .expected(exactly( clause({                            num(0) > 0 , x0 + -x1 + g(x0,x1) != x2 + -g(x3,x2) })))
      .premiseRedundant(false)
    )

TEST_GENERATION_WITH_SUGAR(bug05,
    SUGAR(Real),
    Generation::AsymmetricTest()
      .indices({ inequalityResolutionIdx() })
      .input   (         clause({ -f(x0 + 3 * a) > 0 }))
      .context ({        clause({  f(x1 + a0   ) > 0 })
                ,        clause({  f(x1 + a1   ) > 0 })
                ,        clause({  f(x2 + a2   ) > 0 }) 
                ,        clause({  f(a  + a3   ) > 0 }) 
                ,        clause({  f(b  + a3   ) > 0 }) 
                })
      .expected(exactly( clause({         num(0) > 0 , x0 + 3 * a != x3 + a0 })
                       , clause({         num(0) > 0 , x0 + 3 * a != x4 + a1 })
                       , clause({         num(0) > 0 , x0 + 3 * a != x5 + a2 })
                       , clause({         num(0) > 0 , x0 + 3 * a != a  + a3 })
                       , clause({         num(0) > 0 , x0 + 3 * a != b  + a3 })
                       ))
      .premiseRedundant(false)
    )

// [       is ]: [ $greater(0.0, 0.0) | ~((((15.0 * X0) + ((-15.0 * X1) + g((15.0 * X0), X1))) = ((15.0 * X2) + $uminus(g(X3, X2))))) ]
// [ expected ]: [ $greater(0.0, 0.0) | ~(((((15.0 * X0) + (-15.0 * X1)) + g((15.0 * X0), X1)) = ((15.0 * X0) + $uminus(g(X2, X0))))) ]