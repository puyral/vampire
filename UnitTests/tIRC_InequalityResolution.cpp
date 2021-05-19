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
  DECL_FUNC(f, {Num}, Num)                                                                                    \
  DECL_FUNC(g, {Num, Num}, Num)                                                                               \
  DECL_CONST(a, Num)                                                                                          \
  DECL_CONST(b, Num)                                                                                          \
  DECL_CONST(c, Num)                                                                                          \
  DECL_PRED(r, {Num,Num})                                                                                     \

#define MY_SYNTAX_SUGAR SUGAR(Rat)

#define UWA_MODE Options::UnificationWithAbstraction::ONE_INTERP

Indexing::Index* inequalityResolutionIdx() 
{ return new InequalityResolutionIndex(new TermSubstitutionTree(UWA_MODE, true)); }

InequalityResolution testInequalityResolution() 
{ return InequalityResolution(testIrcState()); }

REGISTER_GEN_TESTER(Test::Generation::GenerationTester<InequalityResolution>(testInequalityResolution()))

/////////////////////////////////////////////////////////
// Basic tests
//////////////////////////////////////

TEST_GENERATION(basic01,
    Generation::TestCase()
      .indices({ inequalityResolutionIdx() })
      .input   (  clause({selected( f(x) > 0 ), x == 7   }) )
      .context ({ clause({selected(-f(x) > 0 )           }) })
      .expected(exactly(
            clause({ num(0) > 0,  x == 7  })
      ))
      .premiseRedundant(false)
    )

TEST_GENERATION(basic02,
    Generation::TestCase()
      .indices({ inequalityResolutionIdx() })
      .input   (  clause({selected(     f(a) > 0) }) )
      .context ({ clause({selected(a + -f(a) > 0) }) })
      .expected(exactly(
            clause({  a > 0  })
      ))
      .premiseRedundant(false)
    )

TEST_GENERATION(basic03,
    Generation::TestCase()
      .indices({ inequalityResolutionIdx() })
      .input   (  clause({selected( -g(x,a) + -g(g(a,b), f(x)) > 0) }) )
      .context ({ clause({selected(  g(b,a) +  g(g(a,b), f(a)) > 0) }) })
      .expected(exactly(
            clause({  g(b,a) + (-g(a,a))  > 0  })
      ))
      .premiseRedundant(false)
    )

TEST_GENERATION(basic04,
    Generation::TestCase()
      .indices({ inequalityResolutionIdx() })
      .input   (  clause({ selected( a + -f(x) > 0), x == 7 }) )
      .context ({ clause({ selected( a +  f(a) > 0) }) })
      .expected(exactly(
            clause({  2 * a > 0, a == 7  })
      ))
      .premiseRedundant(false)
    )

TEST_GENERATION(basic05,
    Generation::TestCase()
      .indices({ inequalityResolutionIdx() })
      .input   (  clause({ selected( a + -f(y) > 0) }) )
      .context ({ clause({ selected( a +  f(a) > 0), x == 7}) })
      .expected(exactly(
            clause({  2 * a > 0, x == 7  })
      ))
      .premiseRedundant(false)
    )

TEST_GENERATION(basic06,
    Generation::TestCase()
      .indices({ inequalityResolutionIdx() })
      .input   (  clause({ selected(       -g(x,y) > 0 ) }) )
      .context ({ clause({ selected( g(a,z) + g(z,a) > 0 ) }) })
      .expected(exactly(
            clause({  g(x,a) > 0 })
          , clause({  g(a,y) > 0 })
      ))
      .premiseRedundant(false)
    )

TEST_GENERATION(basic07,
    Generation::TestCase()
      .indices({ inequalityResolutionIdx() })
      .input   (  clause({ selected(a > 0) }) )
      .context ({ clause({ selected(a > 0) }) })
      .expected(exactly(
      ))
      .premiseRedundant(false)
    )

TEST_GENERATION(basic08,
    Generation::TestCase()
      .indices({ inequalityResolutionIdx() })
      .input   (  clause({ selected(a >= a) }) )
      .context ({ clause({ selected(a > 0 )}) })
      .expected(exactly(
      ))
      .premiseRedundant(false)
    )

TEST_GENERATION(basic09,
    Generation::TestCase()
      .indices({ inequalityResolutionIdx() })
      .input   (  clause({ selected(-a > 0) }) )
      .context ({ clause({           a > 0  }) })
      .expected(exactly(
          clause({ num(0) > 0 })
      ))
      .premiseRedundant(false)
    )


TEST_GENERATION(strictly_max_after_unification_01a,
    Generation::TestCase()
      .indices({ inequalityResolutionIdx() })
      .input   (  clause({ selected(-f(x) + f(a) > 0) }) )
      .context ({ clause({ selected( f(a)        > 0) }) })
      .expected(exactly(
      ))
      .premiseRedundant(false)
    )


TEST_GENERATION(strictly_max_after_unification_01b,
    Generation::TestCase()
      .indices({ inequalityResolutionIdx() })
      .input   (  clause({ selected( f(a)        > 0) })  )
      .context ({ clause({ selected(-f(x) + f(a) > 0) }) })
      .expected(exactly(
      ))
      .premiseRedundant(false)
    )

TEST_GENERATION(strictly_max_after_unification_02a,
    Generation::TestCase()
      .indices({ inequalityResolutionIdx() })
      .input   (         clause({ selected(-f(x) + f(a) > 0 )}) )
      .context ({        clause({ selected( f(b)        > 0) }) })
      .expected(exactly( clause({           f(a)        > 0 }) ))
      .premiseRedundant(false)
    )

TEST_GENERATION(strictly_max_after_unification_02b,
    Generation::TestCase()
      .indices({ inequalityResolutionIdx() })
      .input   (         clause({ selected( f(b)        > 0) })  )
      .context ({        clause({ selected(-f(x) + f(a) > 0 )}) })
      .expected(exactly( clause({           f(a)        > 0 }) ))
      .premiseRedundant(false)
    )

/////////////////////////////////////////////////////////
// Testing gcd for int
//////////////////////////////////////

TEST_GENERATION_WITH_SUGAR(gcd01_Int,
    SUGAR(Int),
    Generation::TestCase()
      .indices({ inequalityResolutionIdx() })
      .input   (  clause({ selected(-6 * f(x) + b > 0) })  )
      .context ({ clause({ selected( 4 * f(y) + a > 0) }) })
      .expected(exactly(
            clause({  2 * b  + 3 * a + -1 > 0 })
      ))
      .premiseRedundant(false)
    )

TEST_GENERATION_WITH_SUGAR(gcd01_Rat,
    SUGAR(Rat),
    Generation::TestCase()
      .indices({ inequalityResolutionIdx() })
      .input   (  clause({ selected( 6 * f(x) + b > 0) })  )
      .context ({ clause({ selected(-4 * f(y) + a > 0) }) })
      .expected(exactly(
            clause({  b  + frac(3,2) * a  > 0 })
      ))
      .premiseRedundant(false)
    )

/////////////////////////////////////////////////////////
// Testing substitution application
//////////////////////////////////////

TEST_GENERATION(substitution01,
    Generation::TestCase()
      .indices({ inequalityResolutionIdx() })
      .input   (  clause({ selected(-f(f(x)) + f(x) > 0) })  )
      .context ({ clause({ selected( f(f(a))        > 0) }) })
      .expected(exactly(
            clause({  f(a) > 0 })
      ))
      .premiseRedundant(false)
    )

TEST_GENERATION(substitution02,
    Generation::TestCase()
      .indices({ inequalityResolutionIdx() })
      .input   (  clause({ selected(-g(f(x), f(f(b))) +    f(x)  > 0) })  )
      .context ({ clause({ selected( g(f(a), f(f(y))) +    f(y)  > 0) }) })
      .expected(exactly(
            clause({  f(a) + f(b) > 0 })
      ))
      .premiseRedundant(false)
    )

/////////////////////////////////////////////////////////
// Testing abstraction
//////////////////////////////////////

TEST_GENERATION(abstraction1,
    Generation::TestCase()
      .indices({ inequalityResolutionIdx() })
      .input   (  clause({ selected(-f(     0        ) > 0) })  )
      .context ({ clause({ selected( f(f(a) + g(b, c)) > 0) }) })
      .expected(exactly(
            clause({ num(0) > 0, f(a) + g(b, c) != 0 })
      ))
      .premiseRedundant(false)
    )



/////////////////////////////////////////////////////////
// Testing normalization
//////////////////////////////////////

TEST_GENERATION(normalization01,
    Generation::TestCase()
      .indices({ inequalityResolutionIdx() })
      .input   (  clause({ selected(0 > f(a)    ) }) )
      .context ({ clause({ selected(a + f(a) > 0) }) })
      .expected(exactly(
            clause({  a > 0  })
      ))
      .premiseRedundant(false)
    )

TEST_GENERATION_WITH_SUGAR(normalization02_Int,
    SUGAR(Int),
    Generation::TestCase()
      .indices({ inequalityResolutionIdx() })
      .input   (  clause({selected(~(0 > -f(a)))  }) )
      //                           ~(0 > -f(a)))
      //                     ==> -f(a) >= 0         ==> 0 >=  f(a)
      //                     ==> -f(a) + 1 > 0      
      .context ({ clause({selected(a + f(a) > 0) }) }) // ==> a + f(a) >  f(a)
                                                       // ==> a        > 0
      .expected(exactly(
            clause({  a > 0  })
      ))
      .premiseRedundant(false)
    )

TEST_GENERATION_WITH_SUGAR(normalization02_Rat,
    SUGAR(Rat),
    Generation::TestCase()
      .indices({ inequalityResolutionIdx() })
      .input   (  clause({selected(~(0 > -f(a)))  }) )
      .context ({ clause({selected( a + f(a) > 0) }) })
      .expected(exactly(
            clause({  a > 0  })
      ))
      .premiseRedundant(false)
    )

// only for integers (which we r using here)
TEST_GENERATION_WITH_SUGAR(normalization03,
    SUGAR(Int),
    Generation::TestCase()
      .indices({ inequalityResolutionIdx() })
      .input   (  clause({selected(     f(a) >= 0) }) )
      .context ({ clause({selected(a + -f(a) >  0) }) })
      .expected(exactly(
            clause({  a   > 0  })
      ))
      .premiseRedundant(false)
    )

TEST_GENERATION_WITH_SUGAR(bug01a,
    SUGAR(Real),
    Generation::TestCase()
      .indices({ inequalityResolutionIdx() })
      .input   (  clause({ r(x, y), selected( (f(x) + -f(y) > 0) ) }) )
      .context ({ clause({ selected(f(a) >  0) }) })
      //                                      (y - 1 > 0) 
      .expected(exactly(
      ))
      .premiseRedundant(false)
    )

TEST_GENERATION_WITH_SUGAR(bug01b,
    SUGAR(Real),
    Generation::TestCase()
      .indices({ inequalityResolutionIdx() })
      .input   (  clause({ g(x, y) == 1, selected( (f(x) + -f(y) > 0) ) }) )
      .context ({ clause({ selected(f(a) >  0) }) })
      //                                      (y - 1 > 0) 
      .expected(exactly(
            clause({     f(x) > 0, g(x, a) == 1 })
      ))
      .premiseRedundant(false)
    )

TEST_GENERATION_WITH_SUGAR(bug02,
    SUGAR(Real),
    Generation::TestCase()
      .indices({ inequalityResolutionIdx() })
      .input   (         clause({ selected( 3 +  a  > 0 ) })  )
      .context ({        clause({ selected( 0 + -a  > 0 ) }) })
      .expected(exactly( clause({            num(3) > 0   }) ))
      .premiseRedundant(false)
    )

TEST_GENERATION_WITH_SUGAR(bug03a,
    SUGAR(Real),
    Generation::TestCase()
// *cl2 = ~P(X1,X2) | 1 + -X1 + a > 0
// *resolvent = $greater($sum(1,$uminus(X1)),0) | ~'MS'(X0,X1,s2)
      .indices({ inequalityResolutionIdx() })
      .input   (         clause({            selected(1 + -f(a)        > 0) })  )
      .context ({        clause({  ~r(y,z) , selected(1 + -f(x) + f(a) > 0) }) })
      .expected(exactly(                      ))
      .premiseRedundant(false)
    )

TEST_GENERATION_WITH_SUGAR(bug03b,
    SUGAR(Real),
    Generation::TestCase()
// *cl2 = ~P(X1,X2) | 1 + -X1 + a > 0
// *resolvent = $greater($sum(1,$uminus(X1)),0) | ~'MS'(X0,X1,s2)
      .indices({ inequalityResolutionIdx() })
      .input   (         clause({                selected(1 + -f(a)        > 0) })  )
      .context ({        clause({  g(y,z) != 1 , selected(1 + -f(x) + f(a) > 0) }) })
      .expected(exactly( clause({  g(y,z) != 1 ,          2 + -f(x)        > 0  }) ))
      .premiseRedundant(false)
    )


TEST_GENERATION_WITH_SUGAR(bug_overflow_01,
    SUGAR(Real),
    Generation::TestCase()
      .indices({ inequalityResolutionIdx() })
      .input   (  clause({ selected(          num(2) * (1073741824 * a + 536870912) > 0 ) })  )
      .context ({ clause({ selected(num(-1) * num(2) * (1073741824 * a + 536870912) > 0 )   }) })
      .expected(exactly(
          // clause({ num(0) > 0 }) // we don't perform the rule if we overflow
      ))
      .premiseRedundant(false)
    )

  // 2 f13(f14, 1) 1073741824

TEST_GENERATION_WITH_SUGAR(bug_overflow_02,
    SUGAR(Int),
    Generation::TestCase()
      .indices({ inequalityResolutionIdx() })
      .input   (  clause({ selected( 0 < 2 * (f(a) * num(1073741824)) ) })  )
      .context ({ clause({ selected( 3  + -a > 0 )  }) })
      .expected(exactly(
      ))
      .premiseRedundant(false)
    )
