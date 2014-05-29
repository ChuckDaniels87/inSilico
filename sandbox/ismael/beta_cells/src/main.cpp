/* BETA CELLS MODEL */
// Standard Library
#include <iostream>
#include <cstdio>
#include <fstream>
#include <string>
#include <vector>
#include <stdlib.h>
#include <chrono>
#include <iterator>
// Classes 
#include "class/cells.hpp"
#include "class/scaffold.hpp"
#include "class/medium.hpp"
#include "class/pool.hpp"
// Auxiliar Functions
#include "auxi/filem.hpp"
#include "auxi/diffusion.hpp"
#include "auxi/generateMesh.hpp"
// Boost Library
#include <boost/lexical_cast.hpp>
#include <boost/bind.hpp>
// Input/Output
#include <base/io/PropertiesParser.hpp>

// inSilico Library
/// Mesh
#include <base/Unstructured.hpp>
#include <base/mesh/Size.hpp>
#include <base/mesh/MeshBoundary.hpp>
/// Quadrature
#include <base/Quadrature.hpp>
/// FE basis
#include <base/fe/Basis.hpp>
/// Field and DOFs
#include <base/Field.hpp>
#include <base/dof/numbering.hpp>
#include <base/dof/Distribute.hpp>
#include <base/dof/generate.hpp>
#include <base/dof/setField.hpp>
#include <base/dof/location.hpp>
#include <base/dof/constrainBoundary.hpp>
/// Integral Kernel
#include <heat/Laplace.hpp>
/// Solver
#include <base/solver/Eigen3.hpp>
/// Assembly
#include <base/asmb/FieldBinder.hpp>
#include <base/asmb/StiffnessMatrix.hpp>
#include <base/asmb/ForceIntegrator.hpp>
#include <base/asmb/BodyForce.hpp>
#include <base/asmb/SimpleIntegrator.hpp>
/// Time integration
#include <base/time/BDF.hpp>
#include <base/time/AdamsMoulton.hpp>
#include <base/time/ReactionTerms.hpp>
#include <base/time/ResidualForceHistory.hpp>


//---------------------------------------


int main ( int argc, char * argv[] )
{
	// Console clearing
	system ( "reset" );

	// Create folders
	system ( "rm -rf ./results" );
	system ( "mkdir ./results" );
	system ( "mkdir ./results/cells" );
	system ( "mkdir ./results/diffusion" );
	
	// Read input file
	if ( argc != 2 && argc != 3 )
	{
		std::cout << "Try this: " << argv[0] << " input.dat\n\n";
		return -1;
	}

	int sarg;
	if ( argc == 2 ){ sarg = 1; }
	else { sarg = 2; }
	
	const std::string inputFile = boost::lexical_cast<std::string> ( argv[ sarg ] );
	int n_cells, n_steps, size_x, size_y, size_z, numElements;
	float step_size;
	double c_cs, c_o2, dmin, dmax, cRate, nutrT, hR, dR;

	{
		// Feed properties
		base::io::PropertiesParser prop;
		prop.registerPropertiesVar ( "N_Cells",			n_cells 	);
		prop.registerPropertiesVar ( "N_Steps",			n_steps 	);
		prop.registerPropertiesVar ( "Step_Size",		step_size 	);
		prop.registerPropertiesVar ( "Size_x",			size_x		);
		prop.registerPropertiesVar ( "Size_y",			size_y		);
		prop.registerPropertiesVar ( "Size_z",			size_z		);
		prop.registerPropertiesVar ( "Num_Elements", 	numElements );
		prop.registerPropertiesVar ( "CS_Conc",			c_cs		);
		prop.registerPropertiesVar ( "O2_Conc",			c_o2		);
		prop.registerPropertiesVar ( "Dmin",			dmin		);
		prop.registerPropertiesVar ( "Dmax",			dmax		);
		prop.registerPropertiesVar ( "Coms_Rate",		cRate		);
		prop.registerPropertiesVar ( "Nutr_Threshold",	nutrT		);
		prop.registerPropertiesVar ( "Health_Rate",		hR			);
		prop.registerPropertiesVar ( "Damage_Rate",		dR			);
		
		// Read Variables
		std::ifstream inp( inputFile.c_str() );
		if ( not inp.is_open() ) { std::cout << 
								   "Cannot open input file\n"; return -1; }
		prop.readValues ( inp );
		inp.close();

		// Make sure all variables have been found
		if ( not prop.isEverythingRead() )
		{
			prop.writeUnread ( std::cerr );
			std::cout << "Some variables are missing\n";
			return -1;
		}
		
	}

	// Start timer
	const auto start_time = std::chrono::steady_clock::now();

	/* DISCRETE MODEL */
	std::cout << "-- DISCRETE MODEL CONFIG --" << std::endl;
	Pool pool;
	
	pool.scaffold.setSize ( size_x, size_y, size_z );
	pool.scaffold.setDiffConst ( dmin, dmax );
	
	pool.medium.setConc ( c_cs, c_o2 );		  

	pool.time_step = step_size * 3600;				 // secs

	pool.addCell ( n_cells );

	Cell::setRate ( cRate );
	Cell::setThreshold ( nutrT );
	Cell::setHPR ( hR, dR );

	std::cout << "Cell addition		\tOK"<< std::endl;
	
	pool.randomPosition();
	std::cout << "Random cells position	\tOK"<< std::endl;
	
	pool.randomSize();
	std::cout << "Random cells size	\tOK"<< std::endl;


	/* DIFFUSION - INSILICO */
	std::cout << "\n-- DIFFUSION MODEL CONFIG --" << std::endl;
	std::cout << "Creating Mesh..." << std::endl;
	// Geometry and Mesh
	const unsigned		dim		= 3;
	const unsigned		geomDeg = 1;
	const base::Shape	shape	= base::HyperCubeShape<dim>::value;		
    const unsigned    	fieldDeg          = 1;
	const unsigned    	kernelDegEstimate = 3;
    const unsigned    	doFSizeC          = 1;
	
	typedef base::Unstructured<shape,geomDeg> Mesh;
	typedef Mesh::Node::VecDim VecDim;

	Mesh mesh;
	{
		     base::Vector<dim,unsigned>::Type  N;
			 N[0] = numElements; N[1] = numElements; N[2] = numElements;
			 VecDim a, b;
			 a[0] = 0;	a[1] = 0;	a[2] = 0;
			 b[0] = size_x; b[1] =  size_y;	b[2] = size_z;
			 generateMesh<dim>( mesh, N, a, b );
	}

    // Mesh size
	const double h = base::mesh::Size<Mesh::Element>::apply( mesh.elementPtr(0) );
	std::cout << "Aprox. Element Size: "<< h << "µm" << std::endl;
	const double elemNumber = std::distance ( mesh.elementsBegin(), mesh.elementsEnd() );
	 std::cout << "Mesh Elements: " << elemNumber << std::endl;
    
	// Quadrature and surface quadrature
	typedef base::Quadrature<kernelDegEstimate,shape> Quadrature;
	Quadrature quadrature;
	std::cout << "Quadrature set		\tOK" << std::endl;
	
	// Time Integration
    typedef base::time::BDF<1> MSM;
    const unsigned nHist = MSM::numSteps;
	std::cout << "Time integration	\tOK" << std::endl;

	// DOF Handling
    typedef base::fe::Basis<shape,fieldDeg>        FEBasis;
    typedef base::Field<FEBasis,doFSizeC,nHist>    Field;
    typedef Field::DegreeOfFreedom                 DoF;
    Field conc_carbon, conc_oxygen;

    // Generate DoFs from mesh
    base::dof::generate<FEBasis>( mesh, conc_carbon);
    base::dof::generate<FEBasis>( mesh, conc_oxygen );

	std::cout << "DOFs Handling		\tOK" << std::endl;
	
	// Locate boundaries
    base::mesh::MeshBoundary meshBoundary;
    meshBoundary.create( mesh.elementsBegin(), mesh.elementsEnd() );
	std::cout << "Boundaries Location	\tOK" << std::endl;
	

	// Set initial conditions
	typedef typename base::Vector<3>::Type VecDimB;  
	base::dof::setField( mesh, conc_carbon,
                         boost::bind( diffusion::initialState<VecDimB, DoF>,
                                      _1, _2 ) );

	std::cout << "Initial Conditions	\tOK" << std::endl;


	// Constrain Boundaries
	base::dof::constrainBoundary<FEBasis>( 	meshBoundary.begin(), 
											meshBoundary.end(),
                                           	mesh, conc_carbon,
                                           	boost::bind( 
											diffusion::boundary<VecDim, DoF, Pool>,
                                                        _1, _2, boost::ref(pool), 1 ) );
													
	std::cout << "Boundary Conditions	\tOK" << std::endl;

	// Number of DOFs
    const std::size_t numDofs =
          base::dof::numberDoFsConsecutively( conc_carbon.doFsBegin(),
                                              conc_carbon.doFsEnd() );

	// Bind Mesh-Field
    typedef base::asmb::FieldBinder<Mesh,Field> FieldBinder;
    FieldBinder fieldBinder( mesh, conc_carbon );
    typedef FieldBinder::TupleBinder<1,1>::Type FTB;
	std::cout << "Bind Mesh - Field	\tOK" << std::endl;


    typedef heat::Laplace<FTB::Tuple> Laplace;
    Laplace laplace ( pool.scaffold.getDiffConst ( 1 ) );
	std::vector<double> elemDiff, elemRate;



	/* ITERATION */
	std::cout << "\n-- CALCULATION PROCESS --" << std::endl;
	for ( int i=0; i < n_steps; ++i )
	{
		std::cout << "\nStep: " << i + 1 << " / " << n_steps << std::endl;
	

		/* Discrete Model */
		std::cout << "   ·Discrete Model:"<< std::endl;
		// Relative Positions
		pool.calculateForce();
		std::cout << "\tForce Calculation\tOK"<< std::endl;
		
		// Move Cells	
		pool.newPosition();
		std::cout << "\tPositions updated\tOK"<< std::endl;

		// Write cells results to file 
		filem::writeVTK ( &pool, i );
		std::cout << "\tWriting in file \tOK"<< std::endl;

	

		/* Difussion */
		std::cout << "   ·Diffusion:"<< std::endl;
		
		// Link cells to mesh
		elemDiff.assign ( elemNumber, pool.scaffold.getDiffConst ( 1 ) ); 
		elemRate.assign ( elemNumber, 0 );

		diffusion::diffusionLink ( pool, mesh, elemDiff, elemRate, numElements );		
		std::cout << "\tLocate Elements\t\tOK"<< std::endl;

		/// Diffusion Constant
		boost::function< double( const Mesh::Element *,
                             	 const Mesh::Element::GeomFun::VecDim & ) >
   		     diffusionFun = boost::bind( &diffusion::diffusionConstant<Mesh::Element,
			 							std::vector<double>>, _1, _2, 
										boost::ref(elemDiff) );
   		
		laplace.setConductivityFunction ( diffusionFun );
		std::cout << "\tDiffusion Cnst. Linked\tOK"<< std::endl;
		


		// Solver Object
        typedef base::solver::Eigen3	Solver;
        Solver solver( numDofs );

		// Compute Rate
		base::asmb::bodyForceComputation2<FTB>(
            quadrature, solver, fieldBinder,
            boost::bind( &diffusion::comsRate<Mesh::Element,Field,
						std::vector<double>>,_1, _2, boost::ref( conc_carbon ),
						boost::ref( elemRate ) ) );
		std::cout << "\tConsumption Rate Linked\tOK"<< std::endl;
 
		// Stiffness Matrix
        base::asmb::stiffnessMatrixComputation<FTB>( quadrature, solver,
                                                     fieldBinder, laplace, 
													 false );
		std::cout << "\tMatrix Computation\tOK"<< std::endl;

		// Inertia Terms
        base::time::computeInertiaTerms<FTB,MSM>( quadrature, solver,
                                                  fieldBinder, step_size, i,
                                                  1.0, false );
		std::cout << "\tInertia Terms\t\tOK"<< std::endl;

		// Finalise
        solver.finishAssembly();
		std::cout << "\tAssembly Finished\tOK"<< std::endl;

		// Solve
        solver.choleskySolve();
		std::cout << "\tCompute and solve\tOK"<< std::endl;

		// Distribute
        base::dof::setDoFsFromSolver( solver, conc_carbon );

		// Pass to History
        base::dof::pushHistory( conc_carbon );

		// Write VTK
		diffusion::writeVTKFile<Mesh,Field> ( "conc_carbon",
												i, mesh,
												conc_carbon );
		std::cout << "\tWriting in file \tOK"<< std::endl;

		// Check Viability
		diffusion::healthLink ( pool, mesh, conc_carbon, numElements );

	};

	
	
	// Print timer
	double time_in_seconds = std::chrono::duration_cast<std::chrono::milliseconds> 
				(std::chrono::steady_clock::now() - start_time).count() / 1000.0;
	std::cout << "\nExecution time: " << time_in_seconds << " seconds\n" << std::endl;

	// Print Log
	std::ofstream logger ( "results/sim.log" );
	if (logger.is_open())
	{
		logger 	<< "CELL SIMULATION LOG" << "\n-------------------\n";
		logger 	<< "\nNºCells: " << n_cells << "\nScaffold Size: [ " << 
				size_x << " " << size_y << " " << size_z << " ]\n\n";
		logger 	<< "NºSteps: " << n_steps << "\nStep Size: " << step_size << " hours\n"
				<< "\nNº Mesh Elements: " << numElements*numElements*numElements
				<< "\nElement Size: " << h << " µm\n";
		logger 	<< "\nNutrient Concentration: " << c_cs << "\nScaffold Diff: " << dmax
				<< "\nCell Diff: " << dmin << "\nConsumption Rate: " << cRate << "\n";
		logger	<< "\nNutrient Threshold: " << nutrT << "\nHealing Rate: " << hR << "\n"
				<< "Damage Rate:" << dR << "\n";
		logger << "\n\nExecution Time: " << time_in_seconds << " seconds\n\n"; 
	}
	logger.close();


	// Open Results in Paraview (option)
	if ( strcmp ( argv[1], "-d" ) == 0 ) 
	{ 
		system ( "paraview ./results/diffusion/conc_carbon_..vtk &" );  
	}
	if ( strcmp ( argv[1], "-c" ) == 0 ) 
	{ 
		system ( "paraview ./results/cells/cells_..vtk &" );  
	}

	// Finish All
//	system ( "read -p \"Press any key...\" -n 1 -s" );
	system ( "reset" );
	
	return 0;
}
