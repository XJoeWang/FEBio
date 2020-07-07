/*This file is part of the FEBio source code and is licensed under the MIT license
listed below.

See Copyright-FEBio.txt for details.

Copyright (c) 2020 University of Utah, The Trustees of Columbia University in 
the City of New York, and others.

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.*/



#include "stdafx.h"
#include "FESoluteBackflowStabilization.h"
#include "FEFluid.h"
#include "FEBioFluidSolutes.h"

//=============================================================================
BEGIN_FECORE_CLASS(FESoluteBackflowStabilization, FESurfaceLoad)
ADD_PARAMETER(m_sol , "sol");
END_FECORE_CLASS();

//-----------------------------------------------------------------------------
//! constructor
FESoluteBackflowStabilization::FESoluteBackflowStabilization(FEModel* pfem) : FESurfaceLoad(pfem), m_dofW(pfem)
{
    m_sol = 0;
    m_dofC = pfem->GetDOFIndex(FEBioFluidSolutes::GetVariableName(FEBioFluidSolutes::FLUID_CONCENTRATION), 0);
}

//-----------------------------------------------------------------------------
//! initialize
bool FESoluteBackflowStabilization::Init()
{
    if (FESurfaceLoad::Init() == false) return false;
    
    // determine the nr of concentration equations
    FEModel& fem = *GetFEModel();
    DOFS& fedofs = fem.GetDOFS();
    m_dofW.AddVariable(FEBioFluidSolutes::GetVariableName(FEBioFluidSolutes::RELATIVE_FLUID_VELOCITY));
    int MAX_CDOFS = fedofs.GetVariableSize(FEBioFluidSolutes::GetVariableName(FEBioFluidSolutes::FLUID_CONCENTRATION));
    if ((m_sol < 1) || (m_sol > MAX_CDOFS)) return false;
    
    m_dof.AddDofs(m_dofW);
	m_dof.AddVariable(FEBioFluidSolutes::GetVariableName(FEBioFluidSolutes::FLUID_CONCENTRATION));

    FESurface* ps = &GetSurface();
    m_backflow.assign(ps->Nodes(), false);
    m_alpha = 1.0;

    return true;
}

//-----------------------------------------------------------------------------
//! Activate the degrees of freedom for this BC
void FESoluteBackflowStabilization::Activate()
{
    FESurface* ps = &GetSurface();
    
    int dofc = m_dofC + m_sol - 1;
    
    for (int i=0; i<ps->Nodes(); ++i)
    {
        FENode& node = ps->Node(i);
        // mark node as having prescribed DOF
        node.set_bc(dofc, DOF_OPEN);
    }
}

//-----------------------------------------------------------------------------
//! Evaluate and prescribe the resistance pressure
void FESoluteBackflowStabilization::Update()
{
    // determine backflow conditions
    MarkBackFlow();
    
    // prescribe solute backflow constraint at the nodes
    FESurface* ps = &GetSurface();
    
    int dofc = m_dofC + m_sol - 1;
    
    for (int i=0; i<ps->Nodes(); ++i)
    {
        FENode& node = ps->Node(i);
        // set node as having prescribed DOF (concentration at previous time)
        if (node.m_ID[dofc] < -1)
            node.set(dofc, node.get_prev(dofc));
    }
}

//-----------------------------------------------------------------------------
//! evaluate the flow rate across this surface
void FESoluteBackflowStabilization::MarkBackFlow()
{
    // Mark all nodes on this surface to have open concentration DOF
    FESurface* ps = &GetSurface();
    int dofc = m_dofC + m_sol - 1;
    for (int i=0; i<ps->Nodes(); ++i)
    {
        FENode& node = ps->Node(i);
        // mark node as having free DOF
        if (node.m_ID[dofc] < -1) {
            node.set_bc(dofc, DOF_OPEN);
            node.m_ID[dofc] = -node.m_ID[dofc] - 2;
        }
    }

    // Calculate normal flow velocity on each face to determine
    // backflow condition
    vec3d rt[FEElement::MAX_NODES];
    vec3d vt[FEElement::MAX_NODES];
    
    for (int iel=0; iel<m_psurf->Elements(); ++iel)
    {
        FESurfaceElement& el = m_psurf->Element(iel);
        
        // nr integration points
        int nint = el.GaussPoints();
        
        // nr of element nodes
        int neln = el.Nodes();
        
        // nodal coordinates
        for (int i=0; i<neln; ++i) {
            FENode& node = m_psurf->GetMesh()->Node(el.m_node[i]);
            rt[i] = node.m_rt*m_alpha + node.m_rp*(1-m_alpha);
            vt[i] = node.get_vec3d(m_dofW[0], m_dofW[1], m_dofW[2])*m_alphaf + node.get_vec3d_prev(m_dofW[0], m_dofW[1], m_dofW[2])*(1-m_alphaf);
        }
        
        double* Nr, *Ns;
        double* N;
        double* w  = el.GaussWeights();
        
        vec3d dxr, dxs, v;
        double vn = 0;

        // repeat over integration points
        for (int n=0; n<nint; ++n)
        {
            N  = el.H(n);
            Nr = el.Gr(n);
            Ns = el.Gs(n);
            
            // calculate the velocity and tangent vectors at integration point
            dxr = dxs = v = vec3d(0,0,0);
            for (int i=0; i<neln; ++i)
            {
                v += vt[i]*N[i];
                dxr += rt[i]*Nr[i];
                dxs += rt[i]*Ns[i];
            }
            
            vec3d normal = dxr ^ dxs;
            normal.unit();
            vn += (normal*v)*w[n];
        }
        
        if (vn < 0) {
            for (int i=0; i<neln; ++i) {
                FENode& node = m_psurf->GetMesh()->Node(el.m_node[i]);
                if (node.m_ID[dofc] > -1) {
                    node.set_bc(dofc, DOF_PRESCRIBED);
                    node.m_ID[dofc] = -node.m_ID[dofc] - 2;
                }
            }
        }
    }
}

//-----------------------------------------------------------------------------
//! calculate residual
void FESoluteBackflowStabilization::LoadVector(FEGlobalVector& R, const FETimeInfo& tp)
{
    m_alpha = tp.alpha; m_alphaf = tp.alphaf;
}

//-----------------------------------------------------------------------------
//! serialization
void FESoluteBackflowStabilization::Serialize(DumpStream& ar)
{
    FESurfaceLoad::Serialize(ar);
	ar & m_dofW;
	ar & m_dofC;
	ar & m_backflow;
	ar & m_alpha;
	ar & m_alphaf;
}
