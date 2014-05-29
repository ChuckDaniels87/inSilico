#ifndef generatemesh_h
#define generatemesh_h

//------------------------------------------------------------------------------
#include <sstream>
#include <base/verify.hpp>
#include <base/io/smf/Reader.hpp>

#include <tools/meshGeneration/unitCube/unitCube.hpp>
#include <tools/converter/smfAffine/smfAffine.hpp>

//------------------------------------------------------------------------------
template<unsigned DIM, typename MESH>
void generateMesh(
    MESH& mesh,
    const typename base::Vector<DIM,unsigned>::Type&  N,
    const typename base::Vector<DIM,double  >::Type&  a,
    const typename base::Vector<DIM,double  >::Type&  b )
{
    static const unsigned dim = DIM;

    for ( unsigned d = 0; d < dim; d++ )
        VERIFY_MSG( b[d] > a[d], "Right end must be right of left end" );
    
    VERIFY_MSG( MESH::Element::GeomFun::degree == 1,
                "Only linear elements currently supported by unitCube" );

    // number of elements per direction
    const unsigned e1 = N[0];
    const unsigned e2 = (dim > 1? N[1] : 1);
    const unsigned e3 = (dim > 2? N[2] : 1);

    // number of nodes per direction
    const unsigned n1 = e1 + 1;
    const unsigned n2 = (dim > 1? e2 + 1 : 1);
    const unsigned n3 = (dim > 2? e3 + 1 : 1);

    typedef tools::meshGeneration::unitCube::SMF<dim,false> SMF;
    
    std::stringstream buffer;
    SMF::apply( e1, e2, e3, buffer );

    
    // apply an affine transformation to convert [0,1]^d to [a,b]^d
    for ( unsigned d1 = 0; d1 < dim; d1++ ) {
        for ( unsigned d2 = 0; d2 < dim; d2++ ) {
            tools::converter::smfAffine::A(d1,d2) = (d1 == d2 ?  b[d1]-a[d1] : 0.0);
        }
        tools::converter::smfAffine::c[d1] = a[d1];
    }

    std::stringstream buffer2;
    tools::converter::smfAffine::Converter<SMF::shape,1>::apply( buffer, buffer2 );
    
    base::io::smf::readMesh( buffer2, mesh );

    return;
}

//------------------------------------------------------------------------------
template<typename MESH>
void generateMesh( MESH& mesh, const unsigned&  N,
                   const double& a, const double&  b )
{
    static const unsigned dim = MESH::Node::dim;
    typedef typename base::Vector<dim,double  >::Type VecDim;
    typedef typename base::Vector<dim,unsigned>::Type VecIntDim;

    VecDim A, B; VecIntDim NN;
    for ( unsigned d = 0; d < dim; d++ ) {
        A[ d] = a;
        B[ d] = b;
        NN[d] = N;
    }

    generateMesh<dim>( mesh, NN, A, B );
}
#endif