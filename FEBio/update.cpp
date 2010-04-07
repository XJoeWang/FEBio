#include "stdafx.h"
#include "fem.h"
#include "FESolidSolver.h"
#include "FEPoroElastic.h"
#include "FEMicroMaterial.h"
#include "FETrussMaterial.h"

///////////////////////////////////////////////////////////////////////////////
// FUNCTION : FESolidSolver::Update
// Updates the current nodal positions based on the displacement increment
// and line search factor. If bfinal is true, it also updates the current
// displacements
//

void FESolidSolver::Update(vector<double>& ui, double s)
{
	int i, n;

	// get the mesh
	FEMesh& mesh = m_fem.m_mesh;

	// update rigid bodies
	UpdateRigidBodies(ui, s);

	// update flexible nodes
	for (i=0; i<mesh.Nodes(); ++i)
	{
		FENode& node = mesh.Node(i);

		// displacement dofs
		// current position = initial + total at prev conv step + total increment so far + current increment  
		if ((n = node.m_ID[0]) >= 0) node.m_rt.x = node.m_r0.x + m_Ut[n] + m_Ui[n] + s*ui[n];
		if ((n = node.m_ID[1]) >= 0) node.m_rt.y = node.m_r0.y + m_Ut[n] + m_Ui[n] + s*ui[n];
		if ((n = node.m_ID[2]) >= 0) node.m_rt.z = node.m_r0.z + m_Ut[n] + m_Ui[n] + s*ui[n];

		// rotational dofs
		if ((n = node.m_ID[3]) >= 0) node.m_Dt.x = node.m_D0.x + m_Ut[n] + m_Ui[n] + s*ui[n];
		if ((n = node.m_ID[4]) >= 0) node.m_Dt.y = node.m_D0.y + m_Ut[n] + m_Ui[n] + s*ui[n];
		if ((n = node.m_ID[5]) >= 0) node.m_Dt.z = node.m_D0.z + m_Ut[n] + m_Ui[n] + s*ui[n];
	}

	// make sure the prescribed displacements are fullfilled
	int ndis = m_fem.m_DC.size();
	for (i=0; i<ndis; ++i)
	{
		if (m_fem.m_DC[i].IsActive())
		{
			int n    = m_fem.m_DC[i].node;
			int lc   = m_fem.m_DC[i].lc;
			int bc   = m_fem.m_DC[i].bc;
			double s = m_fem.m_DC[i].s;

			FENode& node = mesh.Node(n);

			double g = s*m_fem.GetLoadCurve(lc)->Value();

			switch (bc)
			{
			case 0:
				node.m_rt.x = node.m_r0.x + g;
				break;
			case 1:
				node.m_rt.y = node.m_r0.y + g;
				break;
			case 2:
				node.m_rt.z = node.m_r0.z + g;
				break;
			case 20:
				{
					vec3d dr = node.m_r0;
					dr.x = 0; dr.unit(); dr *= g;

					node.m_rt.y = node.m_r0.y + dr.y;
					node.m_rt.z = node.m_r0.z + dr.z;
				}
				break;
			}
		}
	}

	// enforce the linear constraints
	// TODO: do we really have to do this? Shouldn't the algorithm
	// already guarantee that the linear constraints are satisfied?
	if (m_fem.m_LinC.size() > 0)
	{
		int nlin = m_fem.m_LinC.size();
		list<FELinearConstraint>::iterator it = m_fem.m_LinC.begin();
		double d;
		for (int n=0; n<nlin; ++n, ++it)
		{
			FELinearConstraint& lc = *it;
			FENode& node = mesh.Node(lc.master.node);

			d = 0;
			int ns = lc.slave.size();
			list<FELinearConstraint::SlaveDOF>::iterator si = lc.slave.begin();
			for (int i=0; i<ns; ++i, ++si)
			{
				FENode& node = mesh.Node(si->node);
				switch (si->bc)
				{
				case 0: d += si->val*(node.m_rt.x - node.m_r0.x); break;
				case 1: d += si->val*(node.m_rt.y - node.m_r0.y); break;
				case 2: d += si->val*(node.m_rt.z - node.m_r0.z); break;
				}
			}

			switch (lc.master.bc)
			{
			case 0: node.m_rt.x = node.m_r0.x + d; break;
			case 1: node.m_rt.y = node.m_r0.y + d; break;
			case 2: node.m_rt.z = node.m_r0.z + d; break;
			}
		}
	}


	// update velocity and accelerations
	// for dynamic simulations
	if (m_fem.m_pStep->m_nanalysis == FE_DYNAMIC)
	{
		int N = mesh.Nodes();
		double dt = m_fem.m_pStep->m_dt;
		double a = 4.0 / dt;
		double b = a / dt;
		for (i=0; i<N; ++i)
		{
			FENode& n = mesh.Node(i);
			n.m_at = (n.m_rt - n.m_rp)*b - n.m_vp*a - n.m_ap;
			n.m_vt = n.m_vp + (n.m_ap + n.m_at)*dt*0.5;
		}
	}

	// update poroelastic data
	if (m_fem.m_pStep->m_nModule == FE_POROELASTIC) UpdatePoro(ui, s);

	// update contact
	if (m_fem.m_bcontact) m_fem.UpdateContact();

	// update element stresses
	UpdateStresses();

	// dump all states to the plot file
	// when requested
	if (m_fem.m_pStep->m_nplot == FE_PLOT_MINOR_ITRS) m_fem.m_plot->Write(m_fem);
}

///////////////////////////////////////////////////////////////////////////////
//! Updates the poroelastic data

void FESolidSolver::UpdatePoro(vector<double>& ui, double s)
{
	int i, n;

	FEMesh& mesh = m_fem.m_mesh;

	// update poro-elasticity data
	for (i=0; i<mesh.Nodes(); ++i)
	{
		FENode& node = mesh.Node(i);

		// update nodal pressures
		n = node.m_ID[6];
		if (n >= 0) node.m_pt = 0 + m_Ut[n] + m_Ui[n] + s*ui[n];
	}

	// update poro-elasticity data
	for (i=0; i<mesh.Nodes(); ++i)
	{
		FENode& node = mesh.Node(i);

		// update velocities
		node.m_vt  = (node.m_rt - node.m_rp) / m_fem.m_pStep->m_dt;
	}

	// make sure the prescribed pressures are fullfilled
	int ndis = m_fem.m_DC.size();
	for (i=0; i<ndis; ++i)
	{
		if (m_fem.m_DC[i].IsActive())
		{
			int n    = m_fem.m_DC[i].node;
			int lc   = m_fem.m_DC[i].lc;
			int bc   = m_fem.m_DC[i].bc;
			double s = m_fem.m_DC[i].s;

			FENode& node = mesh.Node(n);

			if (bc == 6) node.m_pt = s*m_fem.GetLoadCurve(lc)->Value();
		}
	}
}

///////////////////////////////////////////////////////////////////////////////
//! Updates the rigid body data

void FESolidSolver::UpdateRigidBodies(vector<double>& ui, double s)
{
	int i, j, lc;

	FEMesh& mesh = m_fem.m_mesh;

	// update rigid bodies
	int* lm;
	double* du;
	vec3d r;
	double w;
	quatd dq;
	for (i=0; i<m_fem.m_nrb; ++i)
	{
		// get the rigid body
		FERigidBody& RB = m_fem.m_RB[i];
		if (RB.m_bActive)
		{
			lm = RB.m_LM;

			du = RB.m_du;

			// first do the displacements
			FERigidBodyDisplacement* pdc;
			for (j=0; j<3; ++j)
			{
				pdc = RB.m_pDC[j];
				if (pdc)
				{
					lc = pdc->lc;
					du[j] = (lc < 0? 0 : pdc->sf*m_fem.GetLoadCurve(lc-1)->Value() - RB.m_Up[j]);
				}
				else du[j] = (lm[j] >=0 ? m_Ui[lm[j]] + s*ui[lm[j]] : 0);
			}

			RB.m_rt.x = RB.m_rp.x + du[0];
			RB.m_rt.y = RB.m_rp.y + du[1];
			RB.m_rt.z = RB.m_rp.z + du[2];

			// next, we do the rotations. We do this seperatly since
			// they need to be interpreted differently than displacements
			for (j=3; j<6; ++j)
			{
				pdc = RB.m_pDC[j];
				if (pdc)
				{
					lc = pdc->lc;
					du[j] = (lc < 0? 0 : m_fem.GetLoadCurve(lc-1)->Value() - RB.m_Up[j]);
				}
				else du[j] = (lm[j] >=0 ? m_Ui[lm[j]] + s*ui[lm[j]] : 0);
			}

			r = vec3d(du[3], du[4], du[5]);
			w = sqrt(r.x*r.x + r.y*r.y + r.z*r.z);
			dq = quatd(w, r);

			RB.m_qt = dq*RB.m_qp;
			RB.m_qt.MakeUnit();

			RB.m_Ut[0] = RB.m_Up[0] + du[0];
			RB.m_Ut[1] = RB.m_Up[1] + du[1];
			RB.m_Ut[2] = RB.m_Up[2] + du[2];
			RB.m_Ut[3] = RB.m_Up[3] + du[3];
			RB.m_Ut[4] = RB.m_Up[4] + du[4];
			RB.m_Ut[5] = RB.m_Up[5] + du[5];
		}
	}

	// update rigid body nodes
	int n;
	vec3d a0, at;
	int N = mesh.Nodes();
	for (i=0; i<N; ++i)
	{
		FENode& node = mesh.Node(i);
		n = node.m_rid;
		if (n >= 0)
		{
			// this is a rigid body node
			FERigidBody& RB = m_fem.m_RB[n];

			a0 = node.m_r0 - RB.m_r0;
			at = RB.m_qt*a0;

			node.m_rt = RB.m_rt + at;
		}
	}

	// update rigid joints
	if (m_fem.m_nrj)
	{
		vec3d c, ra, rb, qa, qb;

		for (i=0; i<m_fem.m_nrj; ++i)
		{
			FERigidJoint& rj = m_fem.m_RJ[i];

			FERigidBody& RBa = m_fem.m_RB[ rj.m_nRBa ];
			FERigidBody& RBb = m_fem.m_RB[ rj.m_nRBb ];

			ra = RBa.m_rt;
			rb = RBb.m_rt;

			qa = rj.m_qa0;
			RBa.m_qt.RotateVector(qa);

			qb = rj.m_qb0;
			RBb.m_qt.RotateVector(qb);

			c = ra + qa - rb - qb;

			rj.m_F = rj.m_L + c*rj.m_eps;
		}
	}
}

///////////////////////////////////////////////////////////////////////////////
// FUNCTION : FEM::UpdateStresses
//  Updates the element stresses
//

void FESolidSolver::UpdateStresses()
{
	FEMesh& mesh = m_fem.m_mesh;

	// update the stresses on all domains
	for (int i=0; i<mesh.Domains(); ++i) mesh.Domain(i).UpdateStresses(m_fem);
}

///////////////////////////////////////////////////////////////////////////////
// FUNCTION: FEM::UpdateContact
//  Update contact data
//

void FEM::UpdateContact()
{
	// loop over all contact interfaces
	for (int i=0; i<ContactInterfaces(); ++i) m_CI[i].Update();
}
