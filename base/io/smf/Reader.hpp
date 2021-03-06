//------------------------------------------------------------------------------
// <preamble>
// </preamble>
//------------------------------------------------------------------------------

//! @file   smf/Reader.hpp
//! @author Thomas Rueberg
//! @date   2012

#ifndef base_io_smf_reader_hpp
#define base_io_smf_reader_hpp

//------------------------------------------------------------------------------
// std   includes
#include <istream>
#include <fstream>
#include <limits>
#include <vector>
// boost includes
#include <boost/algorithm/string/case_conv.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/tokenizer.hpp>
// base includes
#include <base/shape.hpp>
// base/io includes
#include <base/io/Format.hpp> 
// base/io/raw includes
#include <base/io/raw/ascii.hpp>

//------------------------------------------------------------------------------
namespace base{
    namespace io{
        namespace smf{

            template<typename MESH> class Reader;

            //------------------------------------------------------------------
            //! Convenenience function
            template<typename MESH>
            void readMesh( std::istream& smf, MESH& mesh )
            {
                Reader<MESH> reader;
                reader( mesh, smf );
            }

            //------------------------------------------------------------------
            namespace detail_{
                
                //! Check if string contains expected value
                template<base::Shape SHAPE>
                struct ValidateShape
                {
                    bool operator()( const std::string & shapeString ) const
                    {
                        // Work with lower-case only
                        const std::string lower =
                            boost::to_lower_copy( shapeString );
                        // Comparison value from shape traits
                        const std::string comp  = base::ShapeName<SHAPE>::apply();
                        
                        return ( lower.find( comp ) != std::string::npos );
                    }
                };

                //! Check if numbers are equal
                template<unsigned NUM>
                struct ValidateNumber
                {
                    bool operator()( const std::string & numberString ) const
                    {
                        // Convert string to number
                        const unsigned number =
                            boost::lexical_cast<unsigned>( numberString );
                        
                        return ( NUM == number );
                    }
                };
                
            } //namespace detail_
        }
    }
}

//------------------------------------------------------------------------------
/** Functor which reads in a mesh from an SMF file
 *
 *  Such a file has the structure
 *  <pre>
 *  Header (including comment lines)
 *  numNodes  numElements
 *  List of coordinates 
 *  List of element connectivities in terms of implicit node numbers
 *  </pre>
 *
 *  \tparam MESH  The mesh to be read in
 */
template<typename MESH>
class base::io::smf::Reader
{
public:
    //! Template parameter: type of mesh
    typedef MESH Mesh;

    //! @name Deduced attributes for header validation and reading
    //@{
    static const base::Shape elementShape     = Mesh::Element::shape;
    static const unsigned    nNodesPerElement = Mesh::Element::numNodes;
    static const unsigned    coordDim         = Mesh::Node::dim;
    //@}

    /** Function call operator doing the main job
     *  \param[out] mesh  The mesh constructed from the file input
     *  \param[in]  smf   Input stream with the SMF-specified geometry
     */
    void operator()( Mesh & mesh, std::istream & smf ) const
    {
        // Read header and validate it
        std::pair<bool,std::string> externalNodes    = std::make_pair( false, "");
        std::pair<bool,std::string> externalElements = std::make_pair( false, "");
        const bool validHeader =
            this -> readAndValidateHeader_( smf, externalNodes, externalElements );
        VERIFY_MSG( validHeader, "Smf header is invalid" );

        unsigned nNodes, nElements;
        this -> readNumbers_( smf, nNodes, nElements );
        mesh.allocate( nNodes, nElements );

        // Read coordinates and pass them to the nodes
        if ( externalNodes.first ) {
            std::ifstream smfNodesExt( externalNodes.second.c_str() );
            
            if ( not smfNodesExt.is_open() ) {
                std::cerr << "Failed to open " << externalNodes.second << '\n';
                VERIFY_MSG( false, "IO Error (see above) " );
            }
                
            this -> readAndSetNodes_( smfNodesExt, mesh );
            smfNodesExt.close();
        }
        else
            this -> readAndSetNodes_( smf, mesh );

        // Read elements' connectivities and pass them to the elements
        if ( externalElements.first ) {
            std::ifstream smfConnExt( externalElements.second.c_str() );

            if ( not smfConnExt.is_open() ) {
                std::cerr << "Failed to open " << externalElements.second << '\n';
                VERIFY_MSG( false, "IO Error (see above) " );
            }

            this -> readAndSetElements_( smfConnExt, mesh );
            smfConnExt.close();
        }
        else 
            this -> readAndSetElements_( smf, mesh );
    }

private:
    
    // Read number of nodes and elements and allocate the mesh
    void readNumbers_( std::istream& smf,
                       unsigned& nNodes, unsigned& nElements ) const
    {
        smf >> nNodes >> nElements;
        smf.ignore( std::numeric_limits<std::streamsize>::max(), '\n' );
        return;
    }


    //--------------------------------------------------------------------------
    /** Read the header of the SMF file and check its validity
     *  - comment lines can appear before or after the header and must begin
     *    with a '#' character
     *  - header lines begin with a '!' character and contain the two data
     *      -  elementShape   SHAPE
     *      -  elementNumPoints NUM
     *    The values of SHAPE and NUM are read and checked against expected
     *    values.
     *  \param[in]  smf               input stream
     *  \param[out] externalNodes     pair of <bool,fileName> for external nodes
     *  \param[out] externalElements  pair of <bool,fileName> for external elemnts
     *  \returns                      success state of reading the header
     */
    bool readAndValidateHeader_( std::istream & smf,
                                 std::pair<bool,std::string>& externalNodes,
                                 std::pair<bool,std::string>& externalElements ) const
    {
        // Skip leading comment lines
        const char commentChar = '#';
        while ( smf.peek() == commentChar )
            smf.ignore( std::numeric_limits<std::streamsize>::max(), '\n' );

        // Check if everything has been found
        bool foundShape  = false;
        bool foundNumber = false;

        // Look for header with element description
        const char headerChar = '!';
        while ( smf.peek() == headerChar )
        {
            // Eat header character
            char dummy;
            smf >> dummy;

            // Read rest of the line
            std::string line;
            getline( smf, line );

            // use only white space characters as seperators
            boost::char_separator<char> sep( " " ); 
            typedef boost::tokenizer< boost::char_separator<char> > Token;
            Token tokens( line, sep );
            Token::iterator iter = tokens.begin();
            const std::string descriptor = *iter;

            if      ( descriptor.find( "elementShape" ) != std::string::npos ) {
                // Read shape from file
                foundShape = true;
                ++iter;
                const std::string value = *iter;

                VERIFY_MSG( detail_::ValidateShape<elementShape>()( value ),
                            x2s( "Smf file has unexpected shape value: " ) +
                            value + x2s( " != " ) +
                            base::ShapeName<elementShape>::apply() );
                
            }
            else if ( descriptor.find( "elementNumPoints" ) != std::string::npos ) {
                // Read number of element nodes from file
                foundNumber = true;
                ++iter;
                const std::string value = *iter;

                VERIFY_MSG( detail_::ValidateNumber<nNodesPerElement>()( value ),
                            x2s( "Smf file has unexpected number of element nodes: " ) +
                            value + x2s( " != " ) +
                            x2s( +nNodesPerElement ) );
                
            }
            else if ( descriptor.find( "externalNodes" ) != std::string::npos ) {
                // external node file to be read
                externalNodes.first  = true;
                ++iter;
                externalNodes.second =  *iter;
            }
            else if ( descriptor.find( "externalElements" ) != std::string::npos ) {
                // external element file to be read
                externalElements.first  = true;
                ++iter;
                externalElements.second =  *iter;
            }
            
        }

        // Skip trailing comment lines
        while ( smf.peek() == commentChar )
            smf.ignore( std::numeric_limits<std::streamsize>::max(), '\n' );

        return (foundShape and foundNumber);
    }

    //--------------------------------------------------------------------------
    /** Read in the nodal coordinates and assign to every node a running number
     *  and the coordinates read from the stream.
     *  \param[in]  smf   Input stream with coordinates
     *  \param[out] mesh  Mesh container to be modified
     */
    void readAndSetNodes_( std::istream & smf, Mesh & mesh ) const
    {

        // Go through all nodes of the mesh
        typename Mesh::NodePtrIter node = mesh.nodesBegin();
        typename Mesh::NodePtrIter end  = mesh.nodesEnd();
        for ( unsigned nodeId = 0; node != end; ++node, nodeId++ ) {
            // Read coordinates from stream
            base::io::raw::readNodeCoordinates( *node, smf );
            // Skip rest of line
            smf.ignore( std::numeric_limits<std::streamsize>::max(), '\n' );
            // Set data of node
            (*node) -> setID( nodeId );
        }

    }

    //--------------------------------------------------------------------------
    /** Read in the element connectivity represented by node indices and
     *  assign node pointers from the mesh's node container with that index
     *  to the pointers held by the element.
     *  \param[in]  smf   Input stream with connectivities
     *  \param[out] mesh  Mesh container to be modified
     */
    void readAndSetElements_( std::istream & smf, Mesh & mesh ) const
    {
        typename Mesh::ElementPtrIter element = mesh.elementsBegin();
        typename Mesh::ElementPtrIter end     = mesh.elementsEnd();

        // Go through all elements
        for ( std::size_t elemID = 0; element != end; ++element, elemID++ ){

            // read node IDs and pass pointers to element
            base::io::raw::readElementConnectivity( mesh, *element, smf );

            // Assign ID
            (*element) -> setID( elemID );
            
            smf.ignore( std::numeric_limits<std::streamsize>::max(), '\n' );
        }
        
    }

};
#endif
