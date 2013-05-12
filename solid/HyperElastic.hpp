//------------------------------------------------------------------------------
// <preamble>
// </preamble>
//------------------------------------------------------------------------------

//! @file   HyperElastic.hpp
//! @author Thomas Rueberg
//! @date   2013

#ifndef solid_hyperelastic_hpp
#define solid_hyperelastic_hpp

//------------------------------------------------------------------------------
// boost includes
#include <boost/function.hpp>
// base includes
#include <base/geometry.hpp>
#include <base/linearAlgebra.hpp>
// mat includes
#include <mat/TensorAlgebra.hpp>
// solid includes
#include <solid/Deformation.hpp>

//------------------------------------------------------------------------------
namespace solid{

    template<typename MATERIAL,
             typename FIELDTUPLE>
    class HyperElastic;

}

//------------------------------------------------------------------------------
/** Implementation of the linearised variation of hyperelastic energy.
 *  Starting from a hyperelastic energy function \f$ \psi \f$, the internal
 *  strain energy reads
 *  \f[
 *       W_{int}(u) = \int_{\Omega_0} \psi( F(u) ) d X
 *  \f]
 *  Its first variation in direction \f$ v \f$ becomes
 *  \f[
 *       \delta (W_{int}(u))(v) = \int_\Omega P : \delta(F(u))(v) d X
 *  \f]
 *  with the first Piola-Kirchhoff stress \f$ P = F S \f$, \f$ S \f$ being the
 *  second Piola-Kirchhoff stress. A linearisation of this term, as necessary
 *  for a Newton algorithm, in direction \f$ \Delta u \f$ becomes
 *  \f[
 *      D(\delta W_{int}(u)(v))[ \Delta u ] =
 *          \int_\Omega \frac{\partial v}{\partial X} : C^{eff}(u) :
 *                      \frac{\partial \Delta u}{\partial X} d X
 *  \f]
 *  with the notion of the \e effective elasticity tensor \f$ C^{eff} \f$
 *  as described below in effectiveElasticity_(). The Newton method would read
 *  \f[
 *      D(\delta W_{int}(u^n)(v))[ \Delta u ] =
 *                                  - \delta W_{int}(u^n)(v) + F^{ext}(v)
 *  \f]
 *  with the update rule \f$ u^{n+1} = u^n + \Delta u \f$. In this object the
 *  the left hand side is implemented in tangentStiffness() and the right hand
 *  side (including the negative sign!) in residualForce(). External forces are
 *  computed elsewhere.
 *  \tparam MATERIAL     Material delivering elasticity tensor and 2nd PK
 *  \tparam FIELDTUPLE   Tuple of field element pointers *
 */
template<typename MATERIAL, typename FIELDTUPLE>
class solid::HyperElastic
{
public:
    //! @name Template parameters
    //@{
    typedef MATERIAL     Material;
    typedef FIELDTUPLE   FieldTuple;
    //@}

    //! @name Extract element types from pointers
    //@{
    typedef typename FieldTuple::GeomElement      GeomElement;
    typedef typename FieldTuple::TestElement      TestElement;
    typedef typename FieldTuple::TrialElement     TrialElement;
    //@}

    //! Number of DoFs per vectorial entry
    static const unsigned nDoFs = TestElement::DegreeOfFreedom::size;

    //! Flag for equal test and form functions --> Bubnov-Galerkin
    static const bool bubnov = boost::is_same<TrialElement,
                                              TestElement>::value;
    
    //! Local coordinate
    typedef typename base::GeomTraits<GeomElement>::LocalVecDim  LocalVecDim;

    //! Type of global vector
    typedef typename base::GeomTraits<GeomElement>::GlobalVecDim GlobalVecDim;

    //! Global space dimension
    static const unsigned globalDim = base::GeomTraits<GeomElement>::globalDim;

    //! Constructor with form and test functions 
    HyperElastic( const Material&   material )
       : material_(   material )
    { }

    //--------------------------------------------------------------------------
    /**  Contribution to the element stiffness matrix in a quadrature rule.
     *   The element stiffness matrix for the 
     *   \f[
     *       K[M d+i, N d+k] = \int \phi^M_{,J} C^{eff}_{iJkL} \phi^N_{,L} dX
     *   \f]
     *   This object adds the weighted integrand evaluated at a local coordinate
     *   \f$\xi\f$ to a provided storage.
     *   \param[in]  fieldTuple Tuple of field element pointers
     *   \param[in]  xi       Local coordinate: quadrature point
     *   \param[in]  weight   Weight corresponding to the quadrature point
     *   \param[out] matrix   Result storage
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

        // Evaluate gradient of test and trial functions
        std::vector<GlobalVecDim> testGradX, trialGradX;
        const double detJ =
            (testEp -> fEFun()).evaluateGradient( geomEp, xi, testGradX );
        
        if ( bubnov ) trialGradX = testGradX;
        else
            (trialEp -> fEFun()).evaluateGradient( geomEp, xi, trialGradX );

        // Sizes and sanity checks
        const unsigned numRowBlocks = testGradX.size();
        const unsigned numColBlocks = trialGradX.size();
        assert( static_cast<unsigned>( matrix.rows() ) == numRowBlocks * nDoFs );
        assert( static_cast<unsigned>( matrix.cols() ) == numColBlocks * nDoFs );

        // Get deformation gradient of the current state
        typename mat::Tensor F;
        solid::deformationGradient( geomEp, trialEp, xi, F );

        // Material evaluations
        typename mat::Tensor S;      // 2nd PK
        typename mat::ElastTensor C; // elasticity tensor
        material_.secondPiolaKirchhoff( F, S );
        material_.materialElasticityTensor( F, C );

        // loop over the test and trial functions
        for ( unsigned M = 0; M < numRowBlocks; M++ ) { // test functions
            for ( unsigned N = 0; N < numColBlocks; N++ ) { // trial functions

                // loop over the vector components of test and trials
                for ( unsigned i = 0; i < nDoFs; i++ ) { // test fun comp
                    for ( unsigned k = 0; k < nDoFs; k++ ) { // trial fun comp

                        // Inner sum: \phi^M_{,J} C^{eff}_{iJkL} \phi^N_{,L}
                        double sum = 0.;
                        for ( unsigned J = 0; J < nDoFs; J++ ) {
                            for ( unsigned L = 0; L < nDoFs; L++ ) {

                                sum +=
                                    testGradX[M][J] *
                                    (this -> effectiveElasticity_( F, S, C, i, J, k, L ) ) *
                                    trialGradX[N][L];
                            }
                        }

                        sum *= detJ * weight;
                        matrix( M * nDoFs + i, N * nDoFs + k ) += sum;
                        
                    } // j
                }// i
            } // N
        } // M

        return;
    }

    //--------------------------------------------------------------------------
    /** Internal force computation for the latest displacement field.
     *  Delegates call to residualForceHistory() with HIST = 0.
     */
    void residualForce( const FieldTuple&   fieldTuple,
                        const LocalVecDim&  xi,
                        const double        weight,
                        base::VectorD&      vector ) const
    {
        this -> residualForceHistory<0>( fieldTuple,
                                         xi, weight, vector );
    }

    //--------------------------------------------------------------------------
    /** This function computes the residual forces for given displacement field.
     *  Part of the RHS terms in the Newton iteration is the negative virtual
     *  strain energy for the given state of deformation
     *  \f[
     *      F[M d + i] = \int_\Omega P_{iJ}(u_{n-s}) \phi^M_{,J} d x
     *  \f]
     *  The Piola-Kirchhoff stress tensor \f$ P \f$ will be evaluated for a
     *  past displacement field \f$ u_{n-s} \f$, \f$ s \geq 0 \f$.
     *  \tparam HIST  Value of \f$ s \f$ for the displacement history
     *   \param[in]  fieldTuple Tuple of field element pointers
     *   \param[in]  xi       Local coordinate: quadrature point
     *   \param[in]  weight   Weight corresponding to the quadrature point
     *   \param[out] vector   Result storage
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

        const unsigned numRowBlocks = testGradX.size();
        assert( static_cast<unsigned>( vector.size() ) == numRowBlocks * nDoFs );

        // Get deformation gradient of the current state
        typename mat::Tensor F;
        solid::deformationGradientHistory<HIST>( geomEp, trialEp, xi, F );
        
        // Material evaluations
        typename mat::Tensor S;      // 2nd PK
        material_.secondPiolaKirchhoff( F, S );

        // First Piola-Kirchhoff stress tensor P = FS
        typename mat::Tensor P;
        P.noalias() = F * S;

        // loop over the test functions
        for ( unsigned M = 0; M < numRowBlocks; M++ ) { // test functions

            // loop over the vector components of trials
            for ( unsigned i = 0; i < nDoFs; i++ ) { // test fun comp

                double sum = 0.;
                for ( unsigned J = 0; J < nDoFs; J++ )
                    sum += P( i, J ) * testGradX[M][J];

                sum *= detJ * weight;
                
                vector[ M * nDoFs + i ] += sum;
            }
        }
        return;
    }

private:

    //--------------------------------------------------------------------------
    /** Computation of effective elasticity tensor entries.
     *  In the final expression of the linearised virtual strain energy, the
     *  so-called effective elasticity tensor pops up which is composed of
     *  the geometrical (or initial) stress contribution and the material
     *  contribution. In index notation, we have
     *  \f[
     *      C^{eff}_{iJkL} = \delta_{ik} S_{JL} + F_{iA} C_{AJBL} F_{kB}
     *  \f]
     *  using the second Piola-Kirchhoff stress tensor \f$ S \f$, the
     *  deformation gradient tensor \f$ F \f$, and the material elasticity
     *  tensor \f$ C \f$.
     *  Note that \f$ C \f$ is stored in the Voigt notation and therefore the
     *  access index pairs, e.g. \f$ 1 \leq A,J \leq 3 \f$, is converted to
     *  a Voigt index \f$ 1 \leq v \leq 6 \f$.
     *
     *  \param[in]  F        Deformation gradient
     *  \param[in]  S        2nd Piola-Kirchhoff stress tensor
     *  \param[in]  C        Material elasticity tensor
     *  \param[in]  i,J,k,L  Current indices of interest
     */
    double effectiveElasticity_( const typename mat::Tensor&      F,
                                 const typename mat::Tensor&      S,
                                 const typename mat::ElastTensor& C,
                                 const unsigned i, const unsigned J,
                                 const unsigned k, const unsigned L ) const
    {
        // geometrical stress contribution
        double result = ( i == k ? S( J, L ) : 0. );

        // material contribution
        
        for ( unsigned A = 0; A < nDoFs; A++ ) { 
            // first Voigt index
            const unsigned voigt1 = mat::Voigt::apply( A, J );
            
            for ( unsigned B = 0; B < nDoFs; B ++ ) {
                // second Voigt index
                const unsigned voigt2 = mat::Voigt::apply( B, L );
                
                // material elasticity 
                const double cAJBL = C( voigt1, voigt2 );

                // convert with deformation gradient
                result += F( i, A ) * cAJBL * F( k, B );
            }
        }

        return result;
    }
    
private:
    const Material&    material_;   //!< Material behaviour
};

#endif
