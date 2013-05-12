//------------------------------------------------------------------------------
// <preamble>
// </preamble>
//------------------------------------------------------------------------------

//! @file   unitCubeSGF.cpp
//! @author Thomas Rueberg
//! @date   2013

// std includes
#include <vector>
#include <iostream>
// tools includes
#include <tools/meshGeneration/meshGeneration.hpp>
#include <tools/meshGeneration/unitCube/unitCube.hpp>

//------------------------------------------------------------------------------
/** Generate an equi-distant mesh on a unit hypercube.
 *  The user provides the number of elements per direction (the value of these
 *  numbers implies the dimension) and this tool writes the generated grid in
 *  SGF format to stdout.
 */
int main( int argc, char* argv[] )
{
    namespace unitCube = tools::meshGeneration::unitCube;
    
    unsigned dim, n1, n2, n3, e1, e2, e3;
    const bool input =
        unitCube::userInput( argc, argv, dim, n1, n2, n3, e1, e2, e3 );
    
    if ( not input ) return 0;
    
    //--------------------------------------------------------------------------
    // Header with message
    std::cout << "# Generated by nCube, input: ";
    for ( int i = 0; i < argc; i ++ ) std::cout << argv[i] << " ";
    std::cout << "\n"
              << (dim > 0 ? e1 : 0 )<< " "
              << (dim > 1 ? e2 : 0 )<< " "
              << (dim > 2 ? e3 : 0 )<< "\n";

    // write node coordinates
    std::vector<tools::meshGeneration::Point> points;
    unitCube::generatePoints( n1, n2, n3, e1, e2, e3, points );

    tools::meshGeneration::writePoints( points, std::cout );

    return 0;
}
