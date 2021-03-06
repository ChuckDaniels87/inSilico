//------------------------------------------------------------------------------
// <preamble>
// </preamble>
//------------------------------------------------------------------------------

//! @file   cut/stabiliseBasis.hpp
//! @author Thomas Rueberg
//! @date   2014

#ifndef base_cut_stabilisebasis_hpp
#define base_cut_stabilisebasis_hpp

//------------------------------------------------------------------------------
// std  includes
#include <vector>
#include <set>
#include <algorithm>
#include <limits>
#include <bitset>
// base includes
#include <base/shape.hpp>
#include <base/geometry.hpp>
#include <base/dof/Constraint.hpp>

//------------------------------------------------------------------------------
namespace base{
    namespace cut{

        template<typename MESH, typename FIELD>
        void stabiliseBasis( const MESH& mesh,
                             FIELD& field,
                             const std::vector<double>& supportAreas,
                             const std::vector< std::pair<std::size_t,
                             typename MESH::Element::GeomFun::VecDim> >& doFLocation,
                             const double   tolerance      = 1.e-8,
                             const unsigned maxIter        = 10,
                             const double   upperThresholdFactor = 1.0,
                             const double   lowerThreshold =
                             std::numeric_limits<double>::min() );

        //----------------------------------------------------------------------
        namespace detail_{

            //------------------------------------------------------------------
            // Collect all element IDs of every DoF
            template<typename FIELD>
            void collectDoFSupports(
                const FIELD& field,
                std::vector<std::vector<std::size_t> >& doFSupports );

            //------------------------------------------------------------------
            // return true, if all DoFs have an active status
            template<typename FELEMENT>
            bool allDoFsActive( const FELEMENT* fieldEp,
                                const bool primaryOnly = false )
            {
                // iterate over DoFs of element
                typename FELEMENT::DoFPtrConstIter dIter = fieldEp -> doFsBegin();
                typename FELEMENT::DoFPtrConstIter dEnd  = fieldEp -> doFsEnd();

                // in this case only check the VERTEX DoFs
                if ( primaryOnly ) {
                    dEnd = dIter;
                    std::advance( dEnd,
                                  base::NumNFaces<FELEMENT::shape,base::VERTEX>::value );
                }
                 
                for ( ; dIter != dEnd; ++dIter ) {
                    // if a component is NOT ACTIVE, return false
                    for ( unsigned d = 0; d < FELEMENT::DegreeOfFreedom::size; d++ ) {
                        if ( not (*dIter) -> isActive(d) ) return false;
                    }
                }

                return true;
            }

            //------------------------------------------------------------------
            template<typename FIELD>
            void categoriseDoFs(
                FIELD& field,
                const double lowerThreshold, const double upperThreshold,
                const std::vector<double>& supportAreas,
                const std::vector<std::vector<std::size_t> >& doFSupports,
                std::vector<typename FIELD::DegreeOfFreedom*>& toBeConstrained,
                std::vector<std::bitset<FIELD::DegreeOfFreedom::size> >& components );

            //------------------------------------------------------------------
            template<typename MESH, typename FIELD>
            std::size_t findSupportingElement(
                const MESH& mesh, const FIELD& field, 
                const std::size_t doFID,
                const typename base::GeomTraits<typename MESH::Element>::GlobalVecDim& x,
                const std::vector< std::vector<std::size_t> >& doFSupports );

            //------------------------------------------------------------------
            template<typename GEOMELEM, typename FIELDELEM>
            void generateConstraints(
                typename FIELDELEM::DegreeOfFreedom* doFPtr,
                const std::bitset<FIELDELEM::DegreeOfFreedom::size>& component,
                const GEOMELEM* geomEp, const FIELDELEM* fieldEp,
                const typename base::GeomTraits<GEOMELEM>::GlobalVecDim& x, 
                const double tolerance,
                const unsigned maxIter );

            //------------------------------------------------------------------
            template<typename ELEMENT>
            typename ELEMENT::GeomFun::VecDim
            locatePointWrtElement(
                const typename base::GeomTraits<ELEMENT>::GlobalVecDim& x,
                const ELEMENT* ep,
                const double tolerance,
                const unsigned maxIter );

            //------------------------------------------------------------------
            template<typename FIELD>
            void twoRingOfDoF(
                const FIELD& field,
                const std::vector< std::vector<std::size_t> >& doFSupports,
                const std::size_t doFID,
                std::vector<std::size_t>& twoRing );
            
        } // end namespace detail_
    }
}

//------------------------------------------------------------------------------
/** Stabilise the FE basis by extension.
  *  In case of computations with cut-elements a frequent problem is the
  *  numeric instability caused by very small intersection sizes of the support
  *  of a shape function and the physical domain. If this intersection size goes
  *  down to zero, the condition number of the basis (and hence the basis) goes
  *  to infinity. Here a stabilisation method is carried out in which the
  *  degrees of freedom which correspond to affected shape functions are
  *  represented as a linear combination of degrees of freedom which are inside
  *  the domain. The stabilisation is carried out in three steps:
  *
  *  1.  Categorise the degrees of freedom in 'inside', 'outside' or
  *      'degenerate' by the size of their support. If the support size of a
  *      DoF \f$ u_i \f$ is larger than one element, it is categorised as
  *      inside, that size is practically zero, the DoF is outside and else
  *      it is degenerate.
  *  2.  Find a set of interior degrees of freedom which will represent the
  *      degenerate DoFs. Here, an element is sought all of whose DoFs are
  *      labeled inside and, in case of many, lies closest to the considered
  *      degenerate DoF.
  *  3.  The linear combination \f$ u_i = \sum c_{ij} u_j \f$ is constructed
  *      and stored as a constraint.
  *
  *  The coefficients \f$ c_{ij} \f$ are chosen as
  *  \f[
  *         c_{ij} = \phi_j( \xi_i )
  *  \f]
  *  where \f$ \phi_j \f$ is the shape function which correpsonds to the
  *  interior degree of freedom \f$ u_j \f$. The local coordinate
  *  \f$ \xi_i \f$ is computed such that
  *  \f[
  *         x_i = x(\xi_i)
  *  \f]
  *  where \f$ x(.) \f$ is the geometry mapping of the element found in
  *  step 2 above and \f$ x_i \f$ is the physical location of the degenerate
  *  DoF \f$ u_i \f$.
  *  Effectively, the value of \f$ u_i \f$ is understood as an extrapolation
  *  of the FE field \f$ u_j \phi_j \f$ from a chose element to the outside
  *  location \f$ x_i \f$.
  *  \tparam MESH  Type of mesh for geometry representation
  *  \tparam FIELD Type of FE field whose basis has to be stabilised
  *  \param[in]     mesh           Geometry representation
  *  \param[in,out] field          Field to stabilise
  *  \param[in]     supportAreas   Size of the supports
  *  \param[in]     doFLocation    Physical location of every DoF
  *  \param[in]     tolerance      Tolerance for coordinate search
  *  \param[in]     maxIter        Number of iterations for coordiante search
  *  \param[in]     upperThresholdFactor Factor times reference element-size
  *                                for deciding the upper threshold
  *  \param[in]     lowerThreshold Support size below this value renders
  *                                DoFs outside
  */
template<typename MESH, typename FIELD>
void base::cut::stabiliseBasis(
    const MESH& mesh, FIELD& field,
    const std::vector<double>& supportAreas,
    const std::vector< std::pair<std::size_t,
    typename MESH::Element::GeomFun::VecDim> >& doFLocation,
    const double   tolerance,
    const unsigned maxIter,
    const double   upperThresholdFactor,
    const double   lowerThreshold )
{
    // upper threshold shall be the size of one element
    const double upperThreshold = upperThresholdFactor *
        base::RefSize<MESH::Element::shape>::apply()
        - (std::sqrt( std::numeric_limits<double>::epsilon() ) );
    
    // Pre-process: collect the support elements of every DoF
    std::vector< std::vector<std::size_t> > doFSupports;
    detail_::collectDoFSupports( field, doFSupports );

    // 1) Categorise the DoFs
    std::vector<typename FIELD::DegreeOfFreedom*> toBeConstrained;
    std::vector<std::bitset<FIELD::DegreeOfFreedom::size> > components;
    detail_::categoriseDoFs( field, lowerThreshold, upperThreshold, supportAreas,
                             doFSupports, toBeConstrained, components );

    // 2) Go through all degenerate DoFs
    for ( std::size_t c = 0; c < toBeConstrained.size(); c++ ) {
        
        // ID of the DoF
        const std::size_t doFID = toBeConstrained[c] -> getID();

        // Location of the doF
        const typename base::GeomTraits<typename MESH::Element>::GlobalVecDim x = 
            base::Geometry<typename MESH::Element>()(
                mesh.elementPtr( doFLocation[ doFID ].first ),
                doFLocation[ doFID ].second );

        // Find closest inside element which supports this DoF
        const std::size_t elementID = 
            detail_::findSupportingElement( mesh, field, 
                                            toBeConstrained[c] -> getID(),
                                            x, doFSupports );

        // Retrieve element pointers of mesh and field 
        const typename  MESH::Element*  geomEp = mesh.elementPtr(  elementID );
        const typename FIELD::Element* fieldEp = field.elementPtr( elementID );

        // Generate linear constraints for the degenerate DoF
        detail_::generateConstraints( toBeConstrained[c],
                                      components[c],
                                      geomEp, fieldEp, x,
                                      tolerance, maxIter );
    }
    
    return;
}

//==============================================================================
// IMPLEMENTATION OF METHODS IN NAMESPACE detail_
//==============================================================================

//------------------------------------------------------------------------------
/** Collect the IDs of all elements which are in the support of all DoFs.
 *  In order to reduce the searches in the other functions in this context, it
 *  is useful to have an a-priori knowledge of supports of every DoF. Here, in
 *  a brute-force manner, for all DoFs of the field all elements are queried if
 *  one of their DoFs is coincident with the DoF of the outer loop. Every such
 *  element is registered in an dynamic array. For every DoF of the field, this
 *  function provides such an array of element IDs.
 *  \tparam Type of the considered field
 *  \param[in]  field       The field to consider
 *  \param[out] doFSupports Array of all arrays of element IDs in DoF support
 */
template<typename FIELD>
void base::cut::detail_::collectDoFSupports(
    const FIELD& field,
    std::vector< std::vector<std::size_t> >& doFSupports )
{
    // begin and end of DoF range
    typename FIELD::DoFPtrConstIter dIter = field.doFsBegin();
    typename FIELD::DoFPtrConstIter dEnd  = field.doFsEnd();

    // size of the storage
    const std::size_t numDoFs = std::distance( dIter, dEnd );
    doFSupports.resize( numDoFs );

    // go through all DoFs of the field
    for ( ; dIter != dEnd; ++dIter ) {

        // the array of all elements in the support of this DoF
        std::vector<std::size_t> thisDoFsSupport;

        // go through all elements of the field
        typename FIELD::ElementPtrConstIter eIter = field.elementsBegin();
        typename FIELD::ElementPtrConstIter eEnd  = field.elementsEnd();
        for ( ; eIter != eEnd; ++eIter ) {

            bool isInSupport = false;

            // go through all DoFs of the considered element
            typename FIELD::Element::DoFPtrConstIter dIter2 = (*eIter) -> doFsBegin();
            typename FIELD::Element::DoFPtrConstIter dEnd2  = (*eIter) -> doFsEnd();
            for ( ; dIter2 != dEnd2; ++dIter2 ) {
                // Comparison of pointers to DoFs
                if ( (*dIter) == (*dIter2) ) {
                    isInSupport = true;
                    break;
                }
            }

            // register element ID if element is DoF's support
            if ( isInSupport )
                thisDoFsSupport.push_back( (*eIter) -> getID() );
        }

        // store array of element IDs for considered DoF
        doFSupports[ (*dIter) -> getID() ] = thisDoFsSupport;
    }

    return;
}                                         
 

//------------------------------------------------------------------------------
/** Perform the categorisation of the DoFs according to support size.
 *  There are three possible states of a DoF (all components are handled
 *  equally here): it can be ACTIVE, INACTIVE, or CONSTRAINED. Given the
 *  threshold values \f$ s_{lower} \f$ and \f$ s_{upper} \f$, every DoF is
 *  categorised according to its support size
 *  \f[
 *        s = | supp(\phi^i) \cap \Omega |_{\xi}
 *  \f]
 *  where \f$ \phi^i \f$ is the shape function belonging to DoF \f$ i \f$,
 *  \f$ supp \f$ its full support and \f$ \Omega \f$ the physically active part
 *  of the domain. Moreover, this size is considered in the parameter space
 *  \f$ { \xi } \f$. In addition, the DoFs are categorised into \e primary and
 *  \e non-primary DoFs: the primary ones are located at the vertices of the
 *  element. This is necessary, because the support size of the non-primary DoFs
 *  is commonly smaller and more prone to be below the upper threshold.
 *
 *  First, for every element the primary DoFs are checked according
 *  their support size. Afterwards the remaining DoFs are considered.
 *  If all primary DoFs of an element adjacent to the non-primary ones are
 *  active, this DoF is automatically active. Otherwise, it follows the same
 *  categorisation.
 *
 *  \tparam FIELD     Type of field whose basis needs categorisation
 *  \param[in]  field           Field to handle
 *  \param[in]  lowerThreshold  DoFs below this value are inactive
 *  \param[in]  upperThreshold  Upper value, above which DoFs are active
 *  \param[in]  supportAreas    The physically active support of every DoF
 *  \param[out] toBeConstrained Collection of DoFs which to be constrained
 */
template<typename FIELD>
void base::cut::detail_::categoriseDoFs(
    FIELD& field,
    const double lowerThreshold, const double upperThreshold,
    const std::vector<double>& supportAreas,
    const std::vector<std::vector<std::size_t> >&   doFSupports, 
    std::vector<typename FIELD::DegreeOfFreedom*>&  toBeConstrained,
    std::vector<std::bitset<FIELD::DegreeOfFreedom::size> >&components )
{
    // total number of DoFs
    const std::size_t numDoFs = std::distance( field.doFsBegin(), field.doFsEnd() );
    
    // marker if a DoF has already been checked
    std::vector<bool> doFMarker( numDoFs , false );

    //--------------------------------------------------------------------------
    // 1) Check all the primary DoFs
    
    // go through all elements of the field
    typename FIELD::ElementPtrConstIter fEIter = field.elementsBegin();
    typename FIELD::ElementPtrConstIter fEEnd  = field.elementsEnd();
    for ( ; fEIter != fEEnd; ++fEIter ) {

        // go through all primary DoFs of the element 
        typename FIELD::Element::DoFPtrConstIter doFIter = (*fEIter) -> doFsBegin();
        typename FIELD::Element::DoFPtrConstIter doFEnd  = doFIter;
        std::advance( doFEnd, base::NumNFaces<FIELD::Element::shape,base::VERTEX>::value );
        for ( ; doFIter != doFEnd; ++doFIter ) {

            // ID of the DoF
            const std::size_t doFID = (*doFIter) -> getID();

            // if untouched
            if ( not doFMarker[ doFID ] ) {

                std::bitset<FIELD::DegreeOfFreedom::size> component;

                // support area of this DoF
                const double thisArea = supportAreas[ doFID ];

                for ( unsigned d = 0; d < FIELD::DegreeOfFreedom::size; d++ ) {
                    // categorise the DoF based on the support area size
                    if ( thisArea >= upperThreshold ) {
                        // if support area is large enough, activate the DoF
                        if ( not ((*doFIter)->isConstrained(d)) )
                            (*doFIter) -> activate(d);
                    }
                    else if ( not ((*doFIter)->isConstrained(d)) ) {
                        // otherwise, deactive the DoF and change the bool
                        (*doFIter) -> deactivate(d);

                        // if the area is above the lower threshold,
                        // register for constraints
                        if ( thisArea >= lowerThreshold  )
                            component.set(d);
                            //toBeConstrained.push_back( *doFIter );
                    }
                }

                if ( component.any() ) {
                    toBeConstrained.push_back( *doFIter );
                    components.push_back( component );
                }
                
                // DoF has been handled by now
                doFMarker[ doFID ] = true;
            }
            
        } // Finished the primary DoFs
    } // Finished first run over elements

    //--------------------------------------------------------------------------
    // 2) Check all the non-primary DoFs
    typename FIELD::DoFPtrIter dIter = field.doFsBegin();
    typename FIELD::DoFPtrIter dEnd  = field.doFsEnd();
    for ( ; dIter != dEnd; ++dIter ) {

        // ID of the DoF
        const std::size_t doFID = (*dIter) -> getID();
        
        if ( not doFMarker[doFID] ) {

            // check if any element in the support has its primary DoFs active
            bool allPrimaryActive = false;
            const std::vector<std::size_t> doFSupport = doFSupports[ doFID ];
            for ( std::size_t s = 0; s < doFSupport.size(); s++ ) {
                // ID of element in support
                const std::size_t elementID = doFSupport[s];
                // are all primary DoFs of this element active
                allPrimaryActive =
                    detail_::allDoFsActive( field.elementPtr( elementID ),
                                            true );

                if ( allPrimaryActive ) break;
            }
            
            std::bitset<FIELD::DegreeOfFreedom::size> component;

            // categorise the DoF according to support size or above flag
            const double thisArea = supportAreas[ doFID ];

            for ( unsigned d = 0; d < FIELD::DegreeOfFreedom::size; d++ ) {
            
                if ( (thisArea >= upperThreshold) or allPrimaryActive ) {
                    // if support area is large enough, activate the DoF
                    if ( not( (*dIter) -> isConstrained(d) ) ) 
                        (*dIter) -> activate(d);
                }
                //else {
                else if ( not( (*dIter) -> isConstrained(d) ) ) {
                    // otherwise, deactive the DoF and change the bool
                    (*dIter) -> deactivate(d);

                    // if the area is above the lower threshold,
                    // register for constraints
                    if ( thisArea >= lowerThreshold  )
                        component.set(d);
                    //toBeConstrained.push_back( *dIter );
                }
                
            }

            if ( component.any() ) {
                toBeConstrained.push_back( *dIter );
                components.push_back( component );
            }

            // DoF has been handled by now
            doFMarker[ doFID ] = true;

        }
    }
    
    return;
}

//------------------------------------------------------------------------------
/** Find an element in the mesh which has active DoFs and lies close to the
 *  given degenerate DoF.
 *  A DoF is degenerate if its support overlaps with the physical domain, but
 *  this overlap is smaller than some threshold. In order to have numerical
 *  stability, a set of non-degenerate DoFs is sought which lie close to the
 *  degenerate DoF. For this reason, an element whose DoFs are active (i.e.,
 *  not degenerate) is sought that lies close to the considered DoF.
 *  Here, the two-ring of elements around the DoF is considered. Let all
 *  elements in the support of the DoF be called its one-ring. The two-ring
 *  is thus the collection of all one-rings of all DoFs of the one-ring.
 *  Among the elements of the two-ring the once in the original one-ring
 *  are not considered (otherwise the DoF would not be degenerate) and
 *  furthermore, only those element which have exclusively active DoFs are
 *  considered. Now for every of the remaining elements, the one whose
 *  centroid lies closest to the considered DoF is selected.
 *  \tparam     MESH        Type of geometry representation
 *  \tparam     FIELD       Type of field 
 *  \param[in]  mesh        The geometry mesh of the problem
 *  \param[in]  field       The field whose basis needs stabilisation
 *  \param[in]  doFID       The ID of the degenerate DoF
 *  \param[in]  x           The location of the degenerate DoF
 *  \param[in]  doFSupports The support elements of all DoFs of the field
 *  \return 
 */
template<typename MESH, typename FIELD>
std::size_t base::cut::detail_::findSupportingElement(
    const MESH& mesh, const FIELD& field, 
    const std::size_t doFID, 
    const typename base::GeomTraits<typename MESH::Element>::GlobalVecDim& x,
    const std::vector< std::vector<std::size_t> >& doFSupports )
{
    std::vector<std::size_t> candidates;
    base::cut::detail_::twoRingOfDoF( field, doFSupports, doFID, candidates );
    
#if 1
    // try a three-ring
    if ( candidates.size() == 0 ) {

        // doFs surrounding the critical DoF
        std::set<std::size_t> surrounding;

        // find all elements in the support of the DoF
        std::vector<std::size_t> elementsInSupport = doFSupports[ doFID ];

        for ( std::size_t e = 0; e < elementsInSupport.size(); e++ ) {
            // access to element in support
            const typename FIELD::Element* fieldEp = field.elementPtr( elementsInSupport[e] );
            // go through all its DoFs
            typename FIELD::Element::DoFPtrConstIter dIter = fieldEp -> doFsBegin();
            typename FIELD::Element::DoFPtrConstIter dEnd  = fieldEp -> doFsEnd();
            for ( ; dIter != dEnd; ++dIter ) {

                // get DoF ID
                const std::size_t otherDoFID = (*dIter) -> getID();

                // avoid self-check
                if ( otherDoFID != doFID ) surrounding.insert( otherDoFID );
            }
        }

        std::vector<std::size_t> threeRing;
        
        // go through surrounding dofs
        std::set<std::size_t>::const_iterator sIter = surrounding.begin();
        std::set<std::size_t>::const_iterator sEnd  = surrounding.end();
        for ( ; sIter != sEnd; ++sIter ) {
            std::vector<std::size_t> tmp;
            base::cut::detail_::twoRingOfDoF( field, doFSupports, *sIter, tmp );
            threeRing.insert( threeRing.end(), tmp.begin(), tmp.end() );
        }

        // sort range (for application of set_difference)
        //std::sort( elementsInSupport.begin(), elementsInSupport.end() );
        //std::sort( threeRing.begin(), threeRing.end() );
        //
        //// subtract dof-support elements from one-ring
        //std::set_difference( threeRing.begin(), threeRing.end(),
        //                     elementsInSupport.begin(), elementsInSupport.end(),
        //                     std::back_inserter( candidates ) );
        candidates.insert( candidates.end(), threeRing.begin(), threeRing.end() );

    }
#endif
    // Be explicit
    if ( candidates.size() == 0 ) {
        std::cerr << "Cannot find supporting element in element ring around DoF "
                  << doFID << " at ("
                  << x.transpose() << ") "
                  << std::endl;
    }
    
    VERIFY_MSG( candidates.size(), "No candidates!" );

    // go through the elements in one-ring \ support, find closest
    double shortestDist = std::numeric_limits<double>::max();
    std::size_t index   = base::invalidInt;
    for ( std::size_t r = 0; r < candidates.size(); r++ ) {

        // element ID
        const std::size_t elemID = candidates[r];
        
        // centroid of element
        const typename base::GeomTraits<typename MESH::Element>::GlobalVecDim
            centroid = 
            base::Geometry<typename MESH::Element>()(
                mesh.elementPtr( elemID ), 
                base::ShapeCentroid<MESH::Element::shape>::apply() );

        // distance of centroid to location of considered DoF
        const double newDist = (x - centroid).norm();

        // check distances and memorise the closer element
        if ( newDist < shortestDist ) {
            shortestDist = newDist;
            index        = elemID;
        }
        
    }

    // element ID of the element inside and closest to DoF
    return index;
}

//------------------------------------------------------------------------------
/** Generate a linear constraint for a given degenerate DoF.
 *  The constraint reads
 *  \f[
 *       u_i = \sum{j \in J(i)} c_{ij} u_j
 *  \f]
 *  using the set \f$ J(i) \f$ of DoF IDs 'closest' to the degenerate DoF
 *  \f$ u_i \f$. These belong to an element that has been found in the routine
 *  findSupportingElement() . This element \f$ \tau_i \f$ has shape functions
 *  \f$ \phi_j \f$ and, moreover the DoF \f$ u_i \f$ has the location
 *  \f$ x_i \f$. At first, the local coordinate representation of \f$ x_i \f$
 *  is found, such that \f$ x_i = x( \xi_i ) \f$ and then the constraints are
 *  \f[
 *        c_{ij} = \phi_j( \xi_i )
 *  \f]
 *  Pairs of pointers to the DoFs \f$ u_j \f$ from \f$ \tau_i \f$ and the
 *  weights \f$ c_{ij} \f$ are added to the constraint object of \f$ u_i \f$.
 *  \tparam GEOMELEM   Type of geometry element
 *  \tparam FIELDELEM  Type of field element
 *  \param[in] doFPtr    Pointer to the degenerate DoF
 *  \param[in] geomEp    Pointer to the geometry element supporting the DoF
 *  \param[in] fieldEp   Pointer to the field element supporting the DoF
 *  \param[in] x         Physical location of the degenerate DoF
 *  \param[in] tolerance Tolerance in the coordinate search
 *  \param[in] maxIter   Maximal number of iterations in the coordinate search
 *
 */
template<typename GEOMELEM, typename FIELDELEM>
void base::cut::detail_::generateConstraints(
    typename FIELDELEM::DegreeOfFreedom* doFPtr,
    const std::bitset<FIELDELEM::DegreeOfFreedom::size>& component,
    const GEOMELEM* geomEp, const FIELDELEM* fieldEp,
    const typename base::GeomTraits<GEOMELEM>::GlobalVecDim& x, 
    const double tolerance,
    const unsigned maxIter )
{
    // get local coordinate of DoF
    const typename GEOMELEM::GeomFun::VecDim xi =
        detail_::locatePointWrtElement( x, geomEp, tolerance, maxIter );

    // evaluate the element's shape functions at this point
    typename FIELDELEM::FEFun::FunArray phi;
    (fieldEp -> fEFun()).evaluate( fieldEp, xi, phi );

    // go through DoFs of the field element closest to DoF
    typename FIELDELEM::DoFPtrConstIter dIter = fieldEp -> doFsBegin();
    typename FIELDELEM::DoFPtrConstIter dEnd  = fieldEp -> doFsEnd();
    for ( unsigned f = 0; dIter != dEnd; ++dIter, f++ ) {

        // weight of the DoF
        const double weight = phi[f];

        // go through all DoF components
        for ( unsigned d = 0; d < FIELDELEM::DegreeOfFreedom::size; d++ ) {

            if ( component.test(d) ) {
                
                // generate a constraint object
                doFPtr -> makeConstraint( d );
            
                // add the weighted DoFs to constraint
                doFPtr -> getConstraint(d) -> addWeightedDoF( *dIter, d, weight );
            }
        }

    }

    return;
}

//------------------------------------------------------------------------------
/** Find the local coordinates of an element that represent a given coordinate.
 *  The weights of above described stabilisation technique are the evaluation of
 *  an element's shape functions at some local coordinate such that the geometry
 *  representation at this local coordinate gives the location of the degenerate
 *  DoF. In detail, let \f$ \tau_i \f$ be the chosen element in order to support
 *  the degenerate DoF \f$ u_i \f$ which has the physical location \f$ x_i \f$.
 *  Now, the local coordinates \f$ \xi_i \f$ of that element \f$ \tau_i \f$ are
 *  sought such that
 *  \f[
 *        x_i = x_{\tau_i}(\xi_i) = \sum_{j} x^j \phi_j(\xi_i)
 *  \f]
 *  This problem is in general nonlinear (only in case of linear simplex element
 *  it is linear) and needs to be solved by a Newton method
 *  \f[
 *       \frac{\partial x_{\tau_i}}{\partial \xi}|_{\xi^k} \Delta \xi =
 *        x_i - x_{\tau_i}(\xi^k) \quad \xi^{k+1} = x^k + \Delta \xi
 *  \f]
 *  Once the norm of the RHS of the Newton iteration is below a given threshold,
 *  the iterations are terminated and the local coordinates \f$ \xi_i \f$
 *  returned to the caller.
 *  \note This function is similar to base::post::findLocationInElement,
 *        but here the point \f$ x \f$ is outside of the element!
 *
 *  \tparam ELEMENT      Type of element for geometry representation
 *  \param[in] x         Given coordinate whose local representation is sought
 *  \param[in] ep        Pointer to geometry element
 *  \param[in] tolerance Tolerance size of residual
 *  \param[in] maxIter   Maximal number of Newton iterations (avoid infinite loop)
 */
template<typename ELEMENT>
typename ELEMENT::GeomFun::VecDim
base::cut::detail_::locatePointWrtElement(
    const typename base::GeomTraits<ELEMENT>::GlobalVecDim& x,
    const ELEMENT* ep,
    const double tolerance,
    const unsigned maxIter )
{
    // useful typedefs
    typedef typename base::GeomTraits<ELEMENT>::GlobalVecDim GlobalVecDim;
    typedef typename base::GeomTraits<ELEMENT>::LocalVecDim  LocalVecDim;

    // initial guess: the element's centroid
    LocalVecDim xi = base::ShapeCentroid<ELEMENT::shape>::apply();

    // Newton iteration
    for ( unsigned iter = 0; iter < maxIter; iter++ ) {

        // Right hand side
        const GlobalVecDim rhs = x - base::Geometry<ELEMENT>()( ep, xi );

        // Residual norm
        const double residual = rhs.norm();

        // already close enough, quit
        if ( residual < tolerance ) return xi;

        // Get element's contra-variant basis = inverse Jacobi matrix
        typename base::ContraVariantBasis<ELEMENT>::MatDimLDim G;
        base::ContraVariantBasis<ELEMENT>()( ep, xi, G );

        // Compute new increment
        LocalVecDim dXi;
        dXi.noalias() = G.transpose() * rhs;

        // update the coordinate
        xi += dXi;
    }

    // Do not reach this point!
    std::cerr << "(WW) Reached maximal number of iterations\n"
              << "     when trying to find ("
              << x.transpose() << ") in Element "
              << ep -> getID() << std::endl;

    // return the latest local coordinate 
    return xi;
}

//------------------------------------------------------------------------------
template<typename FIELD>
void base::cut::detail_::twoRingOfDoF(
    const FIELD& field,
    const std::vector< std::vector<std::size_t> >& doFSupports,
    const std::size_t doFID,
    std::vector<std::size_t>& twoRing )
{
    // construct a temporary
    std::set<std::size_t> tmp;

    // find all elements in the support of the DoF
    std::vector<std::size_t> elementsInSupport = doFSupports[ doFID ];

    for ( std::size_t e = 0; e < elementsInSupport.size(); e++ ) {
        // access to element in support
        const typename FIELD::Element* fieldEp = field.elementPtr( elementsInSupport[e] );
        // go through all its DoFs
        typename FIELD::Element::DoFPtrConstIter dIter = fieldEp -> doFsBegin();
        typename FIELD::Element::DoFPtrConstIter dEnd  = fieldEp -> doFsEnd();
        for ( ; dIter != dEnd; ++dIter ) {

            // get DoF ID
            const std::size_t otherDoFID = (*dIter) -> getID();

            // avoid self-check
            if ( otherDoFID != doFID ) {

                // find all elements in the support of this other DoF
                const std::vector<std::size_t> otherElementsInSupport
                    = doFSupports[ otherDoFID ];
                    
                // select only inside elements
                for ( std::size_t b = 0; b < otherElementsInSupport.size(); b++ ) {
                    if ( detail_::allDoFsActive( field.elementPtr( otherElementsInSupport[b] )))
                        tmp.insert( otherElementsInSupport[b] );

                }

            }

        } // element DoFs
    } // elements in support
        
    twoRing.insert( twoRing.end(), tmp.begin(), tmp.end() );

    return;
}

#endif

