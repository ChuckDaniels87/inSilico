//------------------------------------------------------------------------------
// <preamble>
// </preamble>
//------------------------------------------------------------------------------

//! @file   odeTest/odeTest.cpp
//! @author Thomas Rueberg
//! @date   2014

#include <iostream>
#include <fstream>
#include <string>
#include <sstream>
#include <boost/lexical_cast.hpp>

#include <base/Unstructured.hpp>
#include <base/Quadrature.hpp>

#include <base/io/Format.hpp>
#include <base/io/smf/Reader.hpp>
#include <base/io/vtk/LegacyWriter.hpp>

#include <base/Field.hpp>
#include <base/dof/numbering.hpp>
#include <base/dof/generate.hpp>
#include <base/dof/setField.hpp>
#include <base/fe/Basis.hpp>
#include <base/dof/Distribute.hpp>

#include <base/asmb/StiffnessMatrix.hpp>
#include <base/asmb/ForceIntegrator.hpp>
#include <base/asmb/NeumannForce.hpp>
#include <base/asmb/FieldBinder.hpp>
#include <base/asmb/BodyForce.hpp>

#include <base/solver/Eigen3.hpp>

#include <base/kernel/Mass.hpp>

#include <base/time/BDF.hpp>
#include <base/time/AdamsMoulton.hpp>
#include <base/time/ReactionTerms.hpp>
#include <base/time/ResidualForceHistory.hpp>
//#include <base/time/derivative.hpp>

//------------------------------------------------------------------------------
static const double m = 1.0;
static const double k = 0.5;

static const double uZero = 1.0;
static const double vZero = 0.0;

static const double omega = std::sqrt( k / m );
static const double A = uZero;
static const double B = vZero / omega;

//------------------------------------------------------------------------------
namespace odeTest{

    //! A simple oscillator
    struct Oscillator
    {
        static double solution( const double time )
        {
            return A * std::cos( omega * time ) + B * std::sin( omega * time );
        }

        static double derivative( const double time )
        {
            return omega * (-A * std::sin( omega * time ) + B * std::cos( omega * time ) );
        }

        static double forceFun( const double time )
        {
            return 0.;
        }
    };


    //--------------------------------------------------------------------------
    base::Vector<1>::Type forceFun( const base::Vector<1>::Type& x,
                                    const double time )
    {
        base::Vector<1>::Type result;
        result[0] = Oscillator::forceFun( time );
        return result;
    }

    //--------------------------------------------------------------------------
    // initial conditions
    template<typename DOF>
    void setInitialSolution( const base::Vector<1>::Type& x, DOF* doFPtr )
    {
        doFPtr -> setValue( 0, Oscillator::solution( 0. ) );
        doFPtr -> pushHistory();
    }

    template<typename DOF>
    void setInitialDerivative( const base::Vector<1>::Type& x, DOF* doFPtr )
    {
        doFPtr -> setValue( 0, Oscillator::derivative( 0. ) );
        doFPtr -> pushHistory();
    }

    int odeTest( int argc, char* argv[] );

}

//------------------------------------------------------------------------------
int odeTest::odeTest( int argc, char* argv[] )
{
    if ( argc != 3 ) {
        
        std::cout << "Usage: " << argv[0] << "  numSteps  deltaT  \n"
                  << '\n'
                  << "Solves  m y'' + k y = 0 with initial conditions\n"
                  << "  y(0) = " << uZero
                  << " and  y'(0) = " << vZero << "  and parameters \n"
                  << " m = " << m << ", k = " << k << "\n\n";
        return 0;
    }

    const unsigned tiOrder  = 3;
    //typedef base::time::BDF<tiOrder> MSM;
    typedef base::time::AdamsMoulton<tiOrder> MSM;
    
    // user input
    const unsigned numSteps = boost::lexical_cast<unsigned>( argv[1] );
    const double   deltaT   = boost::lexical_cast<double>(   argv[2] );

    // FE Stuff
    const unsigned geomDeg  = 1;
    const unsigned fieldDeg = 0;
    const base::Shape shape = base::LINE;
    const unsigned nHist    = MSM::numSteps; 

    // Mesh
    typedef base::Unstructured<shape,geomDeg>     Mesh;

    Mesh mesh;
    {
        std::stringstream buffer;
        buffer << "! elementShape     line \n"
               << "! elementNumPoints 2    \n"
               << "2  1 \n"
               << "0. 0. 0. \n "
               << "1. 0. 0. \n "
               << "0  1 \n";

        base::io::smf::Reader<Mesh> smfReader;
        smfReader( mesh, buffer ); 
    }

    // quadrature objects for volume and surface
    const unsigned kernelDegEstimate = 1;
    typedef base::Quadrature<kernelDegEstimate,shape> Quadrature;
    Quadrature quadrature;

    // Create a field
    const unsigned doFSize = 1;
    typedef base::fe::Basis<shape,fieldDeg>           FEBasis;
    typedef base::Field<FEBasis,doFSize,nHist>        Field;
    typedef Field::DegreeOfFreedom                    DoF;
    Field solution, derivative;

    // generate DoFs from mesh
    base::dof::generate<FEBasis>( mesh, solution   );
    base::dof::generate<FEBasis>( mesh, derivative );

    // Number the degrees of freedom
    std::size_t numDoFs =
        base::dof::numberDoFsConsecutively( solution.doFsBegin(), solution.doFsEnd() );
    numDoFs+=
        base::dof::numberDoFsConsecutively( derivative.doFsBegin(), derivative.doFsEnd(), numDoFs );

    // Definition of the field combination
    typedef base::asmb::FieldBinder<Mesh,Field,Field> FieldBinder;
    typedef FieldBinder::TupleBinder<1,1>::Type       SS;
    typedef FieldBinder::TupleBinder<1,2>::Type       SD;
    typedef FieldBinder::TupleBinder<2,1>::Type       DS;
    typedef FieldBinder::TupleBinder<2,2>::Type       DD;
    
    FieldBinder fieldBinder( mesh, solution, derivative );

    typedef base::kernel::Mass<SD::Tuple> Mass1;  Mass1 mass1( m );
    typedef base::kernel::Mass<DS::Tuple> Mass2;  Mass2 mass2( m );

    typedef base::kernel::Mass<SS::Tuple> Coeff1; Coeff1 coeff1(  k );
    typedef base::kernel::Mass<DD::Tuple> Coeff4; Coeff4 coeff4( -m );
    
    // set initial conditions
    base::dof::setField( mesh, solution, 
                         boost::bind( &setInitialSolution<DoF>, _1, _2 ) );
    base::dof::setField( mesh, derivative, 
                         boost::bind( &setInitialDerivative<DoF>, _1, _2 ) );

    // create table for writing the convergence behaviour of the nonlinear solves
    base::io::Table<5>::WidthArray widths = {{ 10, 10, 10, 10, 10 }};
    base::io::Table<5> table( widths );
    table % "#Time" % "y" % "yh"  % "y'" % "yh'";
    std::cout << table;


    table % 0.0
        % Oscillator::solution( 0.0 )
        % (*(solution.doFsBegin())) -> getValue(0)
        % Oscillator::derivative( 0.0 )
        % (*(derivative.doFsBegin())) -> getValue(0);
    std::cout << table;

    // No incremental analysis -> exclude current field value from inertia terms
    const bool incremental = false;

    //--------------------------------------------------------------------------
    // Loop over time steps
    for ( unsigned n = 0; n < numSteps; n ++ ) {

        const double time = (n+1) * deltaT;

        // Create a solver object
        typedef base::solver::Eigen3           Solver;
        Solver solver( numDoFs );

        //------------------------------------------------------------------
        // system matrix coefficients
        base::asmb::stiffnessMatrixComputation<SS>( quadrature, solver, 
                                                    fieldBinder, coeff1 );//, incremental );
        base::asmb::stiffnessMatrixComputation<DD>( quadrature, solver, 
                                                    fieldBinder, coeff4 );//, incremental );

        // inertia terms
#if 1
        base::time::computeInertiaTerms<SD,MSM>( quadrature, solver,
                                                 fieldBinder, deltaT, n, 1.0, incremental );
        base::time::computeInertiaTerms<DS,MSM>( quadrature, solver,
                                                 fieldBinder, deltaT, n, 1.0, incremental );
#else

        base::time::computeReactionTerms<SD,MSM>( mass1, quadrature, solver,
                                                  fieldBinder, deltaT, n, incremental );
        base::time::computeReactionTerms<DS,MSM>( mass2, quadrature, solver,
                                                  fieldBinder, deltaT, n, incremental );
#endif

        // force history
        base::time::computeResidualForceHistory<SS,MSM>( coeff1, quadrature,
                                                         solver, fieldBinder, n );
        base::time::computeResidualForceHistory<DD,MSM>( coeff4, quadrature,
                                                         solver, fieldBinder, n );

        // Body force
        base::asmb::bodyForceComputation<DD>( quadrature, solver, fieldBinder,
                                              boost::bind( &forceFun, _1, time ) );

        // Finalise assembly
        solver.finishAssembly();

        // Solve
        solver.superLUSolve();
            
        // distribute results back to dofs
        base::dof::setDoFsFromSolver( solver, solution );
        base::dof::setDoFsFromSolver( solver, derivative );

        // push history
        base::dof::pushHistory( solution );
        base::dof::pushHistory( derivative );

        table % time
            % Oscillator::solution( time )
            % (*(solution.doFsBegin())) -> getValue(0)
            % Oscillator::derivative( time )
            % (*(derivative.doFsBegin())) -> getValue(0);

        std::cout << table;
        
        // Finished time steps
        //--------------------------------------------------------------------------
    }

    return 0;
}

//------------------------------------------------------------------------------
int main( int argc, char* argv[] )
{
    return odeTest::odeTest( argc, argv );
}
