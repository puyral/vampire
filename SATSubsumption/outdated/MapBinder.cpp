#include "MapBinder.hpp"
#include <iostream>

namespace SMTSubsumption {


std::ostream& operator<<(std::ostream& o, MapBinderSTL const& binder)
{
  o << "MapBinder { ";
  bool first = true;
  for (auto mapping : binder.bindings()) {
    Var var = mapping.first;
    TermList term = mapping.second;
    if (!first) {
      o << ", ";
    } else {
      first = false;
    }
    o << TermList(var, false).toString() << " -> " << term.toString();
  }
  o << " }";
  return o;
}


}