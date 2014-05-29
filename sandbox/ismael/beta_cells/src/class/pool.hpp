/* POOL CLASS HEADER */

// Guard
#ifndef _H_POOL_
#define _H_POOL_

// Standard Libraries
#include <vector>
#include <iostream>
#include <random>
#include <chrono>
#include <thread>

// Classes
#include "cells.hpp"
#include "scaffold.hpp"
#include "medium.hpp"

class Pool {

public:
// Properties
	std::vector<Cell> cells;	// Cell container
	Scaffold scaffold;			// Scaffold
	Medium medium;				// Medium
	double	time_step;			// Time step in seconds
	int		step;				// Step counter

// Constructor
Pool();
// Destructor
virtual ~Pool();

// Print Pool Info
void printInfo();


// Data manipulation methods
	// Add cell to the container, quantity of cells added can be specified
	void addCell ( int quantity = 1 ); 

	// Randomize position of selected cells
	void randomPosition ( ); 			// All cells in container		
	void randomPosition ( int index );		// Selected cell 

	// Randomize radius of cells 
	void randomSize ( ); 				// All cells in container		
	void randomSize ( int index );			// Selected cell 


// Calculation methods
	// Calculate force & velocity
	void calculateForce ( );
	void calculateForce ( int index );
	// Calculate new position 
	void newPosition ( );
	void newPosition ( int index );

// Health system
	void procHealth ( int index, double concetration );
	
};

#endif
