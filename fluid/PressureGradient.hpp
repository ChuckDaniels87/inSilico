//------------------------------------------------------------------------------
// <preamble>
// </preamble>
//------------------------------------------------------------------------------

//! @file   PressureGradient.hpp
//! @author Thomas Rueberg
//! @date   2014

#ifndef fluid_pressuregradient_hpp
#define fluid_pressuregradient_hpp

//------------------------------------------------------------------------------
// boost includes
#include <boost/function.hpp>
// base includes
#include <base/geometry.hpp>
#include <base/linearAlgebra.hpp>
#include <base/kernel/Laplace.hpp>
// fluid includes
#include <fluid/evaluations.hpp>

//------------------------------------------------------------------------------
namespace fluid{
    template<typename FIELDTUPLE> class PressureGradient;
}

//------------------------------------------------------------------------------
/** Computation of the pressure gradient term of Stokes' system.
 *  Integration by parts yields
 *  \f[
 *     \int_{\Omega} v \cdot \nabla p dx = \int_{\Gamma} v \cdot (p n) ds
 *               - \int_{\Omega} p \nabla \cdot v d x
 *  \f]
 *  The boundary term is commonly included in the natural boundary condition,
 *  i.e., \f$ t = -p n + \mu u_{,n} \f$ (or similar in the divergence form).
 *  The domain integral term is implemented in this functor.
 *  \tparam FIELDTUPLE  Type of tuple of elements for evaluation
 */
template<typename FIELDTUPLE>
class fluid::PressureGradient
{
public:
    //! Template parameter
    typedef FIELDTUPLE FieldTuple;

    //! Sanity check
    STATIC_ASSERT_MSG( FieldTuple::numFields >= 2,
                       "Minimum number of fields violated" );

    //! @name Extract element types from pointers
    //@{
    typedef typename FieldTuple::GeomElement      GeomElement;
    typedef typename FieldTuple::TestElement      TestElement;
    typedef typename FieldTuple::TrialElement     TrialElement;
    //@}
    
    //! Number of DoFs per vectorial entry
    static const unsigned nDoFs = TestElement::DegreeOfFreedom::size;

    //! Spatial dimension
    static const unsigned globalDim = GeomElement::Node::dim;
    
    //! Local coordinate
    typedef typename base::GeomTraits<GeomElement>::LocalVecDim  LocalVecDim;

    //! Type of global vector
    typedef typename base::GeomTraits<GeomElement>::GlobalVecDim GlobalVecDim;

    //--------------------------------------------------------------------------
    /** Implementation of the mixed term in Stokes' system.
     *  \f[
     *      B[ Md +i, N ] = - \int_{\Omega} \phi^M_{,i} \psi^N dx
     *  \f]
     */
    void tangentStiffness( const FieldTuple&     fieldTuple,
                           const LocalVecDim&    xi,
                           const double          weight,
                           base::MatrixD&        matrix ) const
    {
        // Extract element pointer from tuple
        const GeomElement*  geomEp  = fieldTuple.geomElementPtr();
        const TestElement*  testEp  = fieldTuple.testElementPtr();
        const TrialElement* trialEp = fieldTuple.trialElementPtr();

        // Evaluate gradient of test functions
        std::vector<GlobalVecDim> testGradX;
        const double detJ =
            (testEp -> fEFun()).evaluateGradient( geomEp, xi, testGradX );

        // Evaluate trial functions
        typename TrialElement::FEFun::FunArray trialFun;
        (trialEp ->  fEFun()).evaluate( geomEp, xi, trialFun );

        // Sizes and sanity checks
        const unsigned numRowBlocks = static_cast<unsigned>( testGradX.size() );
        const unsigned numCols      = static_cast<unsigned>( trialFun.size()  );
        assert( static_cast<unsigned>( matrix.rows() ) == numRowBlocks * nDoFs );
        assert( static_cast<unsigned>( matrix.cols() ) == numCols );

        // compute entries
        for ( unsigned M = 0; M < numRowBlocks; M++ ) {
            for ( unsigned N = 0; N < numCols; N++ ) {

                for ( unsigned d = 0; d < globalDim; d++ )
                    matrix( M*nDoFs + d, N ) +=
                        -detJ * weight * testGradX[M][d] * trialFun[N];
            }
        }

        return;
    }

    //--------------------------------------------------------------------------
    void residualForce( const FieldTuple&  fieldTuple,
                        const LocalVecDim& xi,
                        const double       weight,
                        base::VectorD&     vector ) const
    {
        this -> residualForceHistory<0>( fieldTuple, xi, weight, vector );
    }

    //--------------------------------------------------------------------------
    /** Compute the residual forces due to a given pressure field.
     *  
     *  \f[
     *      F[M*d+i] = - \int_\Omega \phi^M_{,i} p^{n-s} dx
     *  \f]
     */
    template<unsigned HIST>
    void residualForceHistory( const FieldTuple&   fieldTuple,
                               const LocalVecDim&  xi,
                               const double        weight,
                               base::VectorD&      vector ) const
    {
        // Extract element pointer from tuple
        const GeomElement*  geomEp  = fieldTuple.geomElementPtr();
        const TestElement*  testEp  = fieldTuple.testElementPtr();
        const TrialElement* trialEp = fieldTuple.trialElementPtr();

        // Evaluate gradient of test and trial functions
        std::vector<GlobalVecDim> testGradX;
        const double detJ =
            (testEp -> fEFun()).evaluateGradient( geomEp, xi, testGradX );

        // get pressure
        const double p = fluid::pressureHistory<HIST>( geomEp, trialEp, xi );
        
        for ( unsigned M = 0; M < testGradX.size(); M++ ) {
            for ( unsigned i = 0; i < nDoFs; i++ ) {
            
                vector[M*globalDim + i] += -testGradX[M][i] * p * detJ * weight;
            }
        }
    }

    //--------------------------------------------------------------------------
    /*  The co-normal pseudo-derivative operator reads
     *  \f[
     *       B_n(p) = p n
     *  \f]
     *  and its discrete counterpart is given by the matrix coefficients
     *  \f[
     *      B[i,M] = -\psi^M n[i]
     *  \f]
     *
     */
    void coNormalDerivative( const FieldTuple&  fieldTuple,
                             const LocalVecDim& xi,
                             const GlobalVecDim& normal,
                             base::MatrixD& result ) const
    {
        // Extract element pointer from tuple
        const GeomElement*   geomEp  = fieldTuple.geomElementPtr();
        const TrialElement*  trialEp = fieldTuple.trialElementPtr();

        typename TrialElement::FEFun::FunArray trialFun;
        (trialEp ->  fEFun()).evaluate( geomEp, xi, trialFun );

        const unsigned numCols      = static_cast<unsigned>( trialFun.size() );

        result = base::MatrixD::Zero( globalDim, numCols );

        for ( unsigned M = 0; M < numCols; M++ ) {
        
            for ( unsigned i = 0; i < globalDim; i++)
                result(i, M) = -trialFun[M] * normal[i];

        }
        
        return;
    }

    //--------------------------------------------------------------------------
    /** The boundary term due to integration by parts.
     *  This term reads
     *  \f[
     *       - \int_\Gamma p n \cdot v d s
     *  \f]
     *  and is discretised as
     *  \f[
     *     F[Md+i] =  - \int_\Gamma \phi^M n[i] p d s
     *  \f]
     *  
     *
     *  \param[in]  fieldTuple Tuple of field element pointers
     *  \param[in]  xi         Local evaluation coordinate
     *  \param[in]  normal     Surface normal \f$ n \f$
     *  \param[out] result     Result container
     */
    void boundaryResidual( const FieldTuple&   fieldTuple,
                           const LocalVecDim&  xi,
                           const GlobalVecDim& normal,
                           base::VectorD&      result ) const
    {
        // Extract element pointer from tuple
        const GeomElement*  geomEp  = fieldTuple.geomElementPtr();
        const TestElement*  testEp  = fieldTuple.testElementPtr();
        const TrialElement* trialEp = fieldTuple.trialElementPtr();

        // Evaluate test functions
        typename TestElement::FEFun::FunArray testFun;
        (testEp -> fEFun()).evaluate( geomEp, xi, testFun );

        const std::size_t numRowBlocks = testFun.size();

        // initialise result container
        result = base::VectorD::Zero( numRowBlocks * globalDim );

        const double pressure =
            base::post::evaluateField( geomEp, trialEp, xi )[0];
        
        //
        for ( std::size_t M = 0; M < numRowBlocks; M++ ) {
            for ( unsigned i = 0; i < nDoFs; i++ ) {

                result[M*nDoFs+i] = -testFun[M] * normal[i] * pressure;
            }
        }

        return;
    }

};

#endif
