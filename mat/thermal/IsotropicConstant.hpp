//------------------------------------------------------------------------------
// <preamble>
// </preamble>
//------------------------------------------------------------------------------

//! @file   IsotropicConstant.hpp
//! @author Thomas Rueberg
//! @date   2013

#ifndef mat_thermal_isotropicconstant_hpp
#define mat_thermal_isotropicconstant_hpp

// mat includes
#include <mat/TensorAlgebra.hpp>

//------------------------------------------------------------------------------
namespace mat{
    namespace thermal{

        class IsotropicConstant;
    }
}

//------------------------------------------------------------------------------
/** Thermal material behaviour which is isotropic and constant.
 */
class mat::thermal::IsotropicConstant
{
public:
    explicit IsotropicConstant( const double kappa )
        : kappa_( kappa )
    { }

    typedef mat::Vector Vector;
    typedef mat::Tensor Tensor;

    /** Conductivity tensor is constant and isotropic
     *  \f[
     *         K_{ij} = \kappa \delta_{ij}
     * \f]
     */
    void conductivity( const double u, const Vector& gradU,
                       Tensor& K ) const
    {
        K = kappa_ * Tensor::Identity();
    }

    // Derivative is zero
    void conductivityGradient( const double u, const Vector& gradU,
                               Tensor& gradK ) const
    {
        gradK = Tensor::Zero();
    }

private:
    const double kappa_; //!< scalar constant conductivity
};

#endif
