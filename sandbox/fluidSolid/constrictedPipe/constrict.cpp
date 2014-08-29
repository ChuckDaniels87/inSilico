//------------------------------------------------------------------------------
// std includes
#include <iostream>
#include <fstream>
#include <string>
// boost includes
#include <boost/lexical_cast.hpp>
// base includes
#include <base/verify.hpp>
// tools includes
#include <tools/converter/smf2xx/SMFHeader.hpp>
#include <tools/converter/smf2xx/Conversion.hpp>
// local includes
#include <tools/converter/smfMap/smfMap.hpp>


//------------------------------------------------------------------------------
// Custom Map
struct Custom
{
    typedef base::Vector<3,double>::Type   Vec;

    Custom( const double alpha )
        : alpha_( alpha )
    {
        // empty
    }

    // [0,1] --> [0,1]
    double cubicFun( const double x ) const
    {
        return 3. * x*x - 2. *x*x*x;
    }


    // Plot for delta = 2./3.
    //
    //      +-----------+-----------+-----------+-----------+-----------+
    //    1 **************************          +           +          ++
    //      |                        **                                 |
    //      |                          *                                |
    //  0.8 ++                          *                              ++
    //      |                            *   cubic change               |
    //      |                             *                             |
    //  0.6 ++                             *                           ++
    //      |                               *                           |
    //      |                                *                          |
    //  0.4 ++                                ***************************
    //      |                                                           |
    //  0.2 ++                                                         ++
    //      |                                                           |
    //      +           +           +           +           +           +
    //    0 ++----------+-----------+-----------+-----------+----------++
    //     -1           0           1           2           3           4
    //
    double boundaryLine( const double x, const double delta ) const
    {
        if ( x <= 1. ) return 1.;
        if ( x >= 2. ) return 1. - delta;

        const double xi = x - 1.;

        return 1. - delta * cubicFun( xi );
    }
        

    Vec operator()( const Vec& x ) const
    {
        const double bdry = boundaryLine( x[0], alpha_ );

        Vec y;
        y[0] = x[0];
        y[1] = x[1] * bdry;
        y[2] = x[2] * bdry;
        return y;
    }

private:
    const double alpha_;
};

//------------------------------------------------------------------------------
/** Read smf formatted file, create a temporary mesh and write transformed mesh
 */
int main( int argc, char * argv[] )
{
    namespace smfMap    = tools::converter::smfMap;
    namespace smf2xx    = tools::converter::smf2xx;
    
    // Sanity check of the number of input arguments
    if ( argc != 3 ) {
        std::cout << "Usage:  "
                  << argv[0] 
                  << " file.smf  delta \n\n";
        return 0;
    }

    // Name of smf input file, its basename and the data output file name
    const std::string smfFileIn  = boost::lexical_cast<std::string>( argv[1] );
    const double      delta      = boost::lexical_cast<double>(      argv[2] );
    const std::string base       = smfFileIn.substr(0, smfFileIn.find( ".smf") );
    const std::string smfFileOut  = base + ".map.smf";

    // Element attributes
    base::Shape elementShape;
    unsigned    elementNumPoints;
    
    {
        // extract data from header
        std::ifstream smf( smfFileIn.c_str() );
        smf2xx::readSMFHeader( smf, elementShape, elementNumPoints );
        smf.close();
    }

    // Input and output file streams
    std::ifstream smfIn(   smfFileIn.c_str() );
    std::ofstream smfOut( smfFileOut.c_str() );

    // write to file for traceback
    smfOut << "# Generated by smfMap    \n";


    smfMap::coordinateMap = Custom( delta );

    // Call generic conversion helper
    smf2xx::Conversion< smfMap::Converter >::apply( elementShape,
                                                    elementNumPoints,
                                                    smfIn, smfOut );

    // Close the streams
    smfIn.close();
    smfOut.close();
    
    return 0;
}