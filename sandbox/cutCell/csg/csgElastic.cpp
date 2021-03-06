//------------------------------------------------------------------------------
// <preamble>
// </preamble>
//------------------------------------------------------------------------------

//! @file   cutCell/csg/csgElastic.cpp
//! @author Thomas Rueberg
//! @date   2014

#define EMBEDFIRST
//#define LINEAR

#include <string>
#include <numeric>
#include <boost/lexical_cast.hpp>
#include <base/dof/location.hpp>

#include <base/verify.hpp>
#include <base/Unstructured.hpp>
#include <base/mesh/MeshBoundary.hpp>
#include <base/mesh/generateBoundaryMesh.hpp>
#include <base/mesh/Size.hpp>

#include <base/io/PropertiesParser.hpp>
#include <base/io/Format.hpp>

#include <base/cut/Cell.hpp>
#include <base/cut/generateSurfaceMesh.hpp>
#include <base/cut/LevelSet.hpp>
#include <base/cut/ComputeSupport.hpp>
#include <base/cut/Quadrature.hpp>
#include <base/cut/extractMeshFromCutCells.hpp>
#include <base/cut/stabiliseBasis.hpp>

#include <base/dof/numbering.hpp>
#include <base/dof/Distribute.hpp>
#include <base/dof/location.hpp>
#include <base/dof/constrainBoundary.hpp>

#include <base/solver/Eigen3.hpp>

#include <base/auxi/FundamentalSolution.hpp>
#include <base/auxi/BoundingBox.hpp> 

#include <base/post/findLocation.hpp>
#include <base/post/Monitor.hpp>
#include <base/post/ErrorNorm.hpp>

#include <base/cut/ImplicitFunctions.hpp> 

#include <mat/hypel/NeoHookeanCompressible.hpp>
#include <mat/hypel/StVenant.hpp>

#include "../../generateMesh.hpp"
#include "../HyperElastic.hpp"
#include "../SurfaceField.hpp"
#include "../ImplicitGeometry.hpp"

namespace cutCell{
    
    const double coordTol = 1.e-6;

    // exact volume and surface area of the CSG geometry
    const double exactV   = 0.353117618052033;
    const double exactA   = 5.78907848036;

    //------------------------------------------------------------------------------
    //! Dirichlet boundary conditions from fundamental solution
    template<typename FUN, typename DOF>
    void dirichletBCFromFSol( const typename FUN::arg1_type x, DOF* doFPtr, FUN fun )
    {
        typename FUN::result_type value = fun( x );

        // fix the box face with x_d = 0
        if ( base::auxi::almostEqualNumbers( x[DOF::size-1], 0. ) ) {
            
            for ( unsigned d = 0; d < DOF::size; d++ ) {
                if ( doFPtr -> isActive(d) ) {
                    doFPtr -> constrainValue( d, value[d] );
                }
            }
        }
        return;
    }

    //------------------------------------------------------------------------------
    //! Dirichlet boundary conditions: clamp bottom face
    template<unsigned DIM, typename DOF>
    void fixBottom( const typename base::Vector<DIM,double>::Type& x,
                    DOF* doFPtr,
                    const base::auxi::BoundingBox<DIM>& bbox )
    {
        const bool isBot = bbox.isOnLowerBoundary( x, DIM-1, coordTol );
        
        if ( not isBot)  return;
        
        for ( unsigned d = 0; d < DOF::size; d++ ) {
            if ( doFPtr -> isActive(d) ) {
                doFPtr -> constrainValue( d, 0.0 );
            }
        }
        
        return;
    }


    //------------------------------------------------------------------------------
    //! Neumann boundary conditions: twising load on top face
    template<unsigned DIM>
    typename base::Vector<DIM,double>::Type
    twistTop( const typename base::Vector<DIM,double>::Type& x,
              const typename base::Vector<DIM,double>::Type& normal,
              const base::auxi::BoundingBox<DIM>& bbox,
              const double value )
    {
        typedef typename base::Vector<DIM,double>::Type VecDim;
        const VecDim zero = base::constantVector<DIM>( 0. );
        
        const bool isTop = bbox.isOnUpperBoundary( x, DIM-1, coordTol );
        
        if ( not isTop)  return zero;
    
        const VecDim surfCentre = bbox.surfaceCentre( DIM-1, false );
        const VecDim surfNormal = bbox.surfaceNormal( DIM-1, false );
        
        const VecDim t =
            value * ( base::crossProduct( x - surfCentre, surfNormal ) );//+ 0.2 * surfNormal );
        
    
        return t;
    }

    int csgElastic( int argc, char* argv[] );
}

//------------------------------------------------------------------------------
/** Analysis of a CSG-modelled geometry (elasticity)
 */
int cutCell::csgElastic( int argc, char* argv[] )
{
    // spatial dimension
    const unsigned    dim = 3;
    typedef base::Vector<dim,double>::Type VecDim;

    //--------------------------------------------------------------------------
    // surfaces
    
    // sphere
    const double rs = 0.65;
    const VecDim cs = base::constantVector<dim>( 0.5 );
    base::cut::Sphere<dim> sphere( rs, cs, true );
    
    // cylinder
    const double rc = 0.3;
    const VecDim cc = cs;
    const VecDim zero =  base::constantVector<dim>(0.);
    VecDim e1 = zero; e1[0] = 1.;
    VecDim e2 = zero; e2[1] = 1.;
    VecDim e3 = zero; e3[2] = 1.;
    base::cut::Cylinder<dim> cyl1( rc, cc, e1, false );
    base::cut::Cylinder<dim> cyl2( rc, cc, e2, false );
    base::cut::Cylinder<dim> cyl3( rc, cc, e3, false );
    
    if ( argc != 3 ) {
        std::cerr << "Usage: " << argv[0] << " N  input.dat\n"
                  << "(Compiled for dim=" << dim << ")\n\n";
        return -1;
    }

    // read name of input file
    const unsigned    numElements = boost::lexical_cast<unsigned>(    argv[1] );
    const std::string   inputFile = boost::lexical_cast<std::string>( argv[2] );

    // read from input file
    double E, nu, xmax, cutThreshold, sX, sY, sZ;
    std::string meshFile;
    unsigned stabilise; // 0 - not, 1 - Höllig
    bool compute;
    unsigned maxIter, loadSteps;
    double tolerance, forceVal;
    {    
        //Feed properties parser with the variables to be read
        base::io::PropertiesParser prop;
        prop.registerPropertiesVar( "E",            E );
        prop.registerPropertiesVar( "nu",           nu );
        
        prop.registerPropertiesVar( "xmax",         xmax );

        prop.registerPropertiesVar( "sX",           sX );
        prop.registerPropertiesVar( "sY",           sY );
        prop.registerPropertiesVar( "sZ",           sZ );

        prop.registerPropertiesVar( "stabilise",    stabilise );
        prop.registerPropertiesVar( "meshFile",     meshFile );
        prop.registerPropertiesVar( "compute",      compute );
        prop.registerPropertiesVar( "cutThreshold", cutThreshold );

        prop.registerPropertiesVar( "maxIter",      maxIter );
        prop.registerPropertiesVar( "tolerance",    tolerance );
        prop.registerPropertiesVar( "loadSteps",    loadSteps );
        prop.registerPropertiesVar( "forceVal",     forceVal );
        
        // Read variables from the input file
        std::ifstream inp( inputFile.c_str()  );
        VERIFY_MSG( inp.is_open(), "Cannot open input file" );
        VERIFY_MSG( prop.readValuesAndCheck( inp ), "Input error" );
        inp.close( );
    }

    // in case of no computation, do not stabilise
    if ( not compute ) stabilise = 0;
    
    // basic attributes of the computation
    const unsigned             geomDeg  = 1;
    const unsigned             fieldDeg = 1;
    const base::Shape             shape = //base::SimplexShape<dim>::value;
        base::HyperCubeShape<dim>::value;
    const base::Shape surfShape = base::SimplexShape<dim-1>::value;
    const unsigned    kernelDegEstimate = 5;

    // Bulk mesh
    typedef base::Unstructured<shape,geomDeg>  Mesh;
    typedef Mesh::Node::VecDim VecDim;
    Mesh mesh;
    std::string baseName;

    VecDim a, b;
    for ( unsigned d = 0; d < dim; d++ ) {
        a[d] = 0.;
        b[d] = xmax;
    }
    base::auxi::BoundingBox<dim> bbox( a, b );
    
    if ( numElements > 0 ) {
        base::Vector<dim,unsigned>::Type N;
        for ( unsigned d = 0; d < dim; d++ ) {
            N[d] = numElements;
        }
        generateMesh<dim>( mesh, N, a, b );

        baseName = argv[0] + std::string(".") + base::io::leadingZeros( numElements );
    }
    else{
        std::ifstream smf( meshFile.c_str() );
        base::io::smf::readMesh( smf, mesh );

        baseName = base::io::baseName( meshFile, ".smf" );
    }

    // Boundary mesh
    typedef base::mesh::BoundaryMeshBinder<Mesh,true>::Type BoundaryMesh;
    BoundaryMesh boundaryMesh;
    base::mesh::MeshBoundary meshBoundary;
    meshBoundary.create( mesh.elementsBegin(), mesh.elementsEnd() );
    base::mesh::generateBoundaryMesh( meshBoundary.begin(), meshBoundary.end(),
                                      mesh, boundaryMesh );

    // Cell structures
    typedef base::cut::Cell<shape> Cell;
    std::vector<Cell> cells;

    typedef base::cut::Cell<surfShape> SurfCell;
    std::vector<SurfCell> surfCells;

    // intersection of all level sets
    typedef base::cut::LevelSet<dim> LevelSet;
    std::vector<LevelSet> levelSetIntersection;

    //--------------------------------------------------------------------------
    // go through immersed surfaces
    //ImplicitGeometry<Mesh> geometry( mesh, boundaryMesh, sphere );
    cutCell::ImplicitGeometry<Mesh> geometry( mesh, boundaryMesh );
    geometry.intersectAnalytical( sphere, cutThreshold );
    geometry.intersectAnalytical( cyl1, cutThreshold );
    geometry.intersectAnalytical( cyl2, cutThreshold );
    geometry.intersectAnalytical( cyl3, cutThreshold );

    levelSetIntersection = geometry.getLevelSet();

#ifdef EMBEDFIRST
    cells                = geometry.getCells();
    surfCells            = geometry.getSurfCells();
#else
    base::cut::generateCutCells( mesh,         levelSetIntersection, cells,
                                 base::cut::CREATE );
    base::cut::generateCutCells( boundaryMesh, levelSetIntersection, surfCells,
                                 base::cut::CREATE );
#endif

    // Generate a mesh from the immersed surface
    typedef base::cut::SurfaceMeshBinder<Mesh>::SurfaceMesh SurfaceMesh;
    SurfaceMesh surfaceMesh;
    base::cut::generateSurfaceMesh<Mesh,Cell>( mesh, cells, surfaceMesh );

    //--------------------------------------------------------------------------
    // FE
#ifdef LINEAR
    typedef mat::hypel::StVenant               Material;
#else
    typedef mat::hypel::NeoHookeanCompressible Material;
#endif
    
    typedef cutCell::HyperElastic<Mesh,Material,fieldDeg> HyperElastic;
    HyperElastic hyperElastic( mesh, E, nu );

    typedef cutCell::SurfaceField<SurfaceMesh,HyperElastic::Field> SurfaceField;
    SurfaceField surfaceField(      surfaceMesh,         hyperElastic.getField() );
    SurfaceField boundaryField(     boundaryMesh,        hyperElastic.getField() );

    //--------------------------------------------------------------------------
    // Quadratures
    typedef base::cut::Quadrature<kernelDegEstimate,shape> CutQuadrature;
    CutQuadrature cutQuadrature( cells, true );

    typedef base::Quadrature<kernelDegEstimate,surfShape> SurfaceQuadrature;
    SurfaceQuadrature surfaceQuadrature;

    typedef base::cut::Quadrature<kernelDegEstimate,surfShape> SurfaceCutQuadrature;
    SurfaceCutQuadrature surfaceCutQuadrature( surfCells, true );

    // compute supports
    const std::size_t numDoFs = std::distance( hyperElastic.getField().doFsBegin(),
                                               hyperElastic.getField().doFsEnd() );
    std::vector<double> supports;
    supports.resize(  numDoFs );
    
    base::cut::supportComputation( mesh, hyperElastic.getField(), cutQuadrature,  supports );
    std::vector<std::pair<std::size_t,VecDim> > doFLocation;
    base::dof::associateLocation( hyperElastic.getField(), doFLocation );

    // Fundamental solution
#ifdef LINEAR
    HyperElastic::VecDim sourcePoint = base::constantVector<dim>( 0. );
    HyperElastic::VecDim pointForce  = base::constantVector<dim>( 0. );
    sourcePoint[0] = sX; pointForce[0] = 1.;
    if ( dim > 1 ) { sourcePoint[1] = sY; pointForce[1] = 2.; }
    if ( dim > 2 ) { sourcePoint[2] = sZ; pointForce[2] = 3.; }

    typedef base::auxi::FundSolElastoStatic<dim> FSol;
    FSol fSol( mat::Lame::lambda( E, nu ), mat::Lame::mu( E, nu ) );

    typedef boost::function< HyperElastic::VecDoF( const VecDim& ) > FFun;
    FFun fFun = boost::bind( &FSol::fun, &fSol, _1, sourcePoint, pointForce );
    
    // apply dirichlet constraints
    base::dof::constrainBoundary<HyperElastic::FEBasis>(
        meshBoundary.begin(), meshBoundary.end(),
        mesh, hyperElastic.getField(), 
        boost::bind( &dirichletBCFromFSol<FFun,HyperElastic::DoF>, _1, _2, fFun ) );

    typedef boost::function< HyperElastic::VecDoF( const VecDim&,
                                                   const VecDim&) > FFun2;
    FFun2 fFun2 = boost::bind( &FSol::coNormal, &fSol, _1, sourcePoint, pointForce, _2 );
    
#else
    
    base::dof::constrainBoundary<HyperElastic::FEBasis>(
        meshBoundary.begin(), meshBoundary.end(),
        mesh, hyperElastic.getField(), 
        boost::bind( &fixBottom<dim,HyperElastic::DoF>, _1, _2, bbox ) );
    
#endif

    base::cut::stabiliseBasis( mesh, hyperElastic.getField(), supports, doFLocation );

    // number DoFs
    const std::size_t activeDoFsU = 
        base::dof::numberDoFsConsecutively( hyperElastic.getField().doFsBegin(),
                                            hyperElastic.getField().doFsEnd() );

    //--------------------------------------------------------------------------
    // Load step loop
#ifdef LINEAR
    const unsigned numSteps = 1;
    const unsigned numIter  = 1;
#else
    const unsigned numSteps = loadSteps;
    const unsigned numIter  = maxIter;

    // write zero state 
    hyperElastic.writeVTKFile(    baseName, 0, levelSetIntersection, cells, true );
    hyperElastic.writeVTKFileCut( baseName, 0, levelSetIntersection, cells, true );
#endif

    for ( unsigned s = 0; s < numSteps and compute; s++ ) {

        //----------------------------------------------------------------------
        // Nonlinear iterations
        unsigned iter = 0;
        while( iter < numIter ) {

            // Create a solver object
            typedef base::solver::Eigen3           Solver;
            Solver solver( activeDoFsU );

#ifdef LINEAR            
            // Neumann boundary condition -- box boundary
            boundaryField.applyNeumannBoundaryConditions( surfaceCutQuadrature,
                                                          solver, fFun2 );
        
            // Neumann boundary condition -- immersed surface
            surfaceField.applyNeumannBoundaryConditions( surfaceQuadrature,
                                                         solver, fFun2 );
            
#else
            const double factor =
                static_cast<double>( s+1 ) / static_cast<double>( numSteps );
            
            boundaryField.applyNeumannBoundaryConditions(
                surfaceCutQuadrature,
                solver,
                boost::bind( &twistTop<dim>, _1, _2, bbox, forceVal * factor ) );

            
#endif
        
            hyperElastic.assembleBulk( cutQuadrature, solver, iter );
            
            // Finalise assembly
            solver.finishAssembly();

            // norm of residual 
            const double conv1 = solver.norm();
#ifndef LINEAR
            std::cout << s << "  " << iter << "  " << conv1 << "  ";
#endif
            // convergence via residual norm
            if ( conv1 < tolerance * E ) { // note the tolerance multiplier
#ifndef LINEAR
                std::cout << std::endl;
#endif
                break;
            }

            // Solve
            //solver.choleskySolve();
            //solver.cgSolve();
            solver.superLUSolve();
            
            // distribute results back to dofs
            base::dof::addToDoFsFromSolver( solver, hyperElastic.getField() );

            // norm of displacement increment
            const double conv2 = solver.norm();
#ifndef LINEAR
            std::cout << conv2 << std::endl;
#endif
            iter++;
            
            // convergence via increment
            if ( conv2 < tolerance ) break;
            
        } // end iterations
            

        // warning
        if ( iter == maxIter ) {
            std::cout << "# (WW) Step " << s << " has not converged within "
                      << maxIter << " iterations \n";
        }

        //----------------------------------------------------------------------
        // write a vtk file
        hyperElastic.writeVTKFile(    baseName, s+1, levelSetIntersection, cells );
        hyperElastic.writeVTKFileCut( baseName, s+1, levelSetIntersection, cells );
        
    } // end load steps

    //------------------------------------------------------------------------------
    // Compute error and mesh volume for convergence (linear only)
#ifdef LINEAR            
    double hmin;
    {
        std::vector<double> h;
        Mesh::ElementPtrConstIter eIter = mesh.elementsBegin();
        Mesh::ElementPtrConstIter eEnd  = mesh.elementsEnd();
        for ( ; eIter != eEnd; ++eIter )  {
            h.push_back( base::mesh::elementSize( *eIter ) );
        }
     
        hmin = *std::min_element( h.begin(), h.end() );
    }

    std::cout << hmin << "  ";
    
    //--------------------------------------------------------------------------
    // Error
    std::cout << hyperElastic.computeL2Error( cutQuadrature, fFun ) << "  ";
    
    //--------------------------------------------------------------------------
    // Compute mesh volume
    const double volume = hyperElastic.computeVolume( cutQuadrature );
    std::cout << std::abs(volume-exactV) << "  ";

    //--------------------------------------------------------------------------
    // Compute surface area
    double area = surfaceField.computeArea( surfaceQuadrature );
    area += boundaryField.computeArea( surfaceCutQuadrature );
        
    std::cout << std::abs(area-exactA) << "  " << std::endl;

#endif

    return 0;
}

//------------------------------------------------------------------------------
int main( int argc, char* argv[] )
{
    return cutCell::csgElastic( argc, argv );
}
