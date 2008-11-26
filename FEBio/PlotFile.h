// PlotFile.h: interface for the PlotFile class.
//
//////////////////////////////////////////////////////////////////////

#if !defined(AFX_PLOTFILE_H__6E7170ED_6C03_4720_96CF_C53411A7464E__INCLUDED_)
#define AFX_PLOTFILE_H__6E7170ED_6C03_4720_96CF_C53411A7464E__INCLUDED_

#if _MSC_VER > 1000
#pragma once
#endif // _MSC_VER > 1000

#include "Archive.h"

class FEM;

//-----------------------------------------------------------------------------
//! This class implements the facilities to write to a plot database. 

//! FEBio outputs the results of the analysis in the LSDYNA database format,
//! which is also known to as the "plot" file. This class serves as the 
//! interface to this format.

class PlotFile
{
protected:

	//-----------------------------------------------------------------------------
	//! This is the header of the plot database. 
	
	//! The header consists of 64 integers, however
	//! the first 10 integers are to be interpreted as characters (single bytes),
	//! since they contain the title of the problem. Note that the title is not
	//! zero-terminated.

	struct PLOTHEADER
	{
		char	Title[40];		//!< title of the problem
		int		UnUsed0[5];		//!< blanks (unused)
		int		ndim;			//!< number of dimensions
		int		nump;			//!< number of nodal points
		int		icode;			//!< code descriptor
		int		nglbv;			//!< number of global state variables
		int		flagT;			//!< state nodal temperatures included ?
		int		flagU;			//!< state nodal coordinates included ?
		int		flagV;			//!< state nodal velocities included ?
		int		flagA;			//!< state nodal accelerations included ?
		int		nel8;			//!< number of 8-node hexahedral elements
		int		nummat8;		//!< number of materials used by hexahedral elements
		int		UnUsed1[2];		//!< blanks (unused)
		int		nv3d;			//!< number of variables for hexahedral elements
		int		nel2;			//!< number of 2-node beam elements
		int		nummat2;		//!< number of materials used by beam elements
		int		nv1d;			//!< number of variables for beam elements
		int		nel4;			//!< number of 4-node shell elements
		int		nummat4;		//!< number of materials used by shell elements
		int		nv2d;			//!< number of variables for shell elements
		int		neiph;			//!< number of additional variables per solid element
		int		neips;			//!< number of additional variables per shell integration point
		int		maxint;			//!< number of integration points dumped for each shell
		int		UnUsed3[7];		//!< blank (unused)
		int		ioshl1;			//!< 6 stress component flag for shells
		int		ioshl2;			//!< plastic strain flag for shells
		int		ioshl3;			//!< shell force resultant flag
		int		ioshl4;			//!< shell thickness, energy + 2 more
		int		UnUsed4[16];	//!< blank (unused)
	};

public:
	//! constructor
	PlotFile();

	//! descructor
	virtual ~PlotFile();

	//! Open the plot database
	bool Open(FEM& fem, const char* szfile);

	//! Open for appending
	bool Append(FEM& fem, const char* szfile);

	//! close the plot database
	void Close();

	//! Write current FE state to plot database
	bool Write(FEM& fem);

public:
	bool	m_bsstrn;	//!< shell strain flag

protected:
	PLOTHEADER	m_ph;	//!< The plot file header

	Archive	m_ar;		//!< the actual data archive

};

#endif // !defined(AFX_PLOTFILE_H__6E7170ED_6C03_4720_96CF_C53411A7464E__INCLUDED_)
