/*
*	Copyright (C) 2010 Thorsten Liebig (Thorsten.Liebig@gmx.de)
*
*	This program is free software: you can redistribute it and/or modify
*	it under the terms of the GNU General Public License as published by
*	the Free Software Foundation, either version 3 of the License, or
*	(at your option) any later version.
*
*	This program is distributed in the hope that it will be useful,
*	but WITHOUT ANY WARRANTY; without even the implied warranty of
*	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
*	GNU General Public License for more details.
*
*	You should have received a copy of the GNU General Public License
*	along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "engine.h"
#include "engine_extension.h"
#include "operator_extension.h"
#include "tools/array_ops.h"

//! \brief construct an Engine instance
//! it's the responsibility of the caller to free the returned pointer
Engine* Engine::New(const Operator* op)
{
	cout << "Create FDTD engine" << endl;
	Engine* e = new Engine(op);
	e->Init();
	return e;
}

Engine::Engine(const Operator* op)
{
	m_type = BASIC;
	numTS = 0;
	Op = op;
	for (int n=0;n<3;++n)
		numLines[n] = Op->GetOriginalNumLines(n);
	volt=NULL;
	curr=NULL;
}

Engine::~Engine()
{
	this->Reset();
}

void Engine::Init()
{
	numTS = 0;
	volt = Create_N_3DArray<FDTD_FLOAT>(numLines);
	curr = Create_N_3DArray<FDTD_FLOAT>(numLines);

	file_et.open( "et" );
	file_ht.open( "ht" );

	InitExtensions();
	SortExtensionByPriority();
}

void Engine::InitExtensions()
{
	for (size_t n=0;n<Op->GetNumberOfExtentions();++n)
	{
		Operator_Extension* op_ext = Op->GetExtension(n);
		Engine_Extension* eng_ext = op_ext->CreateEngineExtention();
		if (eng_ext)
		{
			eng_ext->SetEngine(this);
			m_Eng_exts.push_back(eng_ext);
		}
	}
}

void Engine::ClearExtensions()
{
	for (size_t n=0;n<m_Eng_exts.size();++n)
		delete m_Eng_exts.at(n);
	m_Eng_exts.clear();
}

bool CompareExtensions(Engine_Extension* i, Engine_Extension* j)
{
	return (*i<*j);
}

void Engine::SortExtensionByPriority()
{
	stable_sort(m_Eng_exts.begin(),m_Eng_exts.end(), CompareExtensions);
	reverse(m_Eng_exts.begin(),m_Eng_exts.end());
}

void Engine::Reset()
{
	Delete_N_3DArray(volt,numLines);
	volt=NULL;
	Delete_N_3DArray(curr,numLines);
	curr=NULL;

	file_et.close();
	file_ht.close();

	ClearExtensions();
}

void Engine::UpdateVoltages(unsigned int startX, unsigned int numX)
{
	unsigned int pos[3];
	bool shift[3];

	pos[0] = startX;
	//voltage updates
	for (unsigned int posX=0;posX<numX;++posX)
	{
		shift[0]=pos[0];
		for (pos[1]=0;pos[1]<numLines[1];++pos[1])
		{
			shift[1]=pos[1];
			for (pos[2]=0;pos[2]<numLines[2];++pos[2])
			{
				shift[2]=pos[2];
				//do the updates here
				//for x
				volt[0][pos[0]][pos[1]][pos[2]] *= Op->vv[0][pos[0]][pos[1]][pos[2]];
				volt[0][pos[0]][pos[1]][pos[2]] += Op->vi[0][pos[0]][pos[1]][pos[2]] * ( curr[2][pos[0]][pos[1]][pos[2]] - curr[2][pos[0]][pos[1]-shift[1]][pos[2]] - curr[1][pos[0]][pos[1]][pos[2]] + curr[1][pos[0]][pos[1]][pos[2]-shift[2]]);

				//for y
				volt[1][pos[0]][pos[1]][pos[2]] *= Op->vv[1][pos[0]][pos[1]][pos[2]];
				volt[1][pos[0]][pos[1]][pos[2]] += Op->vi[1][pos[0]][pos[1]][pos[2]] * ( curr[0][pos[0]][pos[1]][pos[2]] - curr[0][pos[0]][pos[1]][pos[2]-shift[2]] - curr[2][pos[0]][pos[1]][pos[2]] + curr[2][pos[0]-shift[0]][pos[1]][pos[2]]);

				//for z
				volt[2][pos[0]][pos[1]][pos[2]] *= Op->vv[2][pos[0]][pos[1]][pos[2]];
				volt[2][pos[0]][pos[1]][pos[2]] += Op->vi[2][pos[0]][pos[1]][pos[2]] * ( curr[1][pos[0]][pos[1]][pos[2]] - curr[1][pos[0]-shift[0]][pos[1]][pos[2]] - curr[0][pos[0]][pos[1]][pos[2]] + curr[0][pos[0]][pos[1]-shift[1]][pos[2]]);
			}
		}
		++pos[0];
	}
}

void Engine::ApplyVoltageExcite()
{
	int exc_pos;
	unsigned int ny;
	unsigned int pos[3];
	//soft voltage excitation here (E-field excite)
	for (unsigned int n=0;n<Op->Exc->Volt_Count;++n)
	{
		exc_pos = (int)numTS - (int)Op->Exc->Volt_delay[n];
		exc_pos *= (exc_pos>0 && exc_pos<=(int)Op->Exc->Length);
//			if (n==0) cerr << numTS << " => " << Op->ExciteSignal[exc_pos] << endl;
		ny = Op->Exc->Volt_dir[n];
		pos[0]=Op->Exc->Volt_index[0][n];
		pos[1]=Op->Exc->Volt_index[1][n];
		pos[2]=Op->Exc->Volt_index[2][n];
		SetVolt(ny,pos, GetVolt(ny,pos) + Op->Exc->Volt_amp[n]*Op->Exc->Signal_volt[exc_pos]);
	}

	// write the voltage excitation function into the file "et"
	if (numTS < Op->Exc->Length)
		file_et << numTS * Op->GetTimestep() << "\t" << Op->Exc->Signal_volt[numTS] << "\n"; // do not use std::endl here, because it will do an implicit flush
}

void Engine::UpdateCurrents(unsigned int startX, unsigned int numX)
{
	unsigned int pos[3];
	pos[0] = startX;
	for (unsigned int posX=0;posX<numX;++posX)
	{
		for (pos[1]=0;pos[1]<numLines[1]-1;++pos[1])
		{
			for (pos[2]=0;pos[2]<numLines[2]-1;++pos[2])
			{
				//do the updates here
				//for x
				curr[0][pos[0]][pos[1]][pos[2]] *= Op->ii[0][pos[0]][pos[1]][pos[2]];
				curr[0][pos[0]][pos[1]][pos[2]] += Op->iv[0][pos[0]][pos[1]][pos[2]] * ( volt[2][pos[0]][pos[1]][pos[2]] - volt[2][pos[0]][pos[1]+1][pos[2]] - volt[1][pos[0]][pos[1]][pos[2]] + volt[1][pos[0]][pos[1]][pos[2]+1]);

				//for y
				curr[1][pos[0]][pos[1]][pos[2]] *= Op->ii[1][pos[0]][pos[1]][pos[2]];
				curr[1][pos[0]][pos[1]][pos[2]] += Op->iv[1][pos[0]][pos[1]][pos[2]] * ( volt[0][pos[0]][pos[1]][pos[2]] - volt[0][pos[0]][pos[1]][pos[2]+1] - volt[2][pos[0]][pos[1]][pos[2]] + volt[2][pos[0]+1][pos[1]][pos[2]]);

				//for z
				curr[2][pos[0]][pos[1]][pos[2]] *= Op->ii[2][pos[0]][pos[1]][pos[2]];
				curr[2][pos[0]][pos[1]][pos[2]] += Op->iv[2][pos[0]][pos[1]][pos[2]] * ( volt[1][pos[0]][pos[1]][pos[2]] - volt[1][pos[0]+1][pos[1]][pos[2]] - volt[0][pos[0]][pos[1]][pos[2]] + volt[0][pos[0]][pos[1]+1][pos[2]]);
			}
		}
		++pos[0];
	}
}

void Engine::ApplyCurrentExcite()
{
	int exc_pos;
	unsigned int ny;
	unsigned int pos[3];
	//soft current excitation here (H-field excite)
	for (unsigned int n=0;n<Op->Exc->Curr_Count;++n)
	{
		exc_pos = (int)numTS - (int)Op->Exc->Curr_delay[n];
		exc_pos *= (exc_pos>0 && exc_pos<=(int)Op->Exc->Length);
//			if (n==0) cerr << numTS << " => " << Op->ExciteSignal[exc_pos] << endl;
		ny = Op->Exc->Curr_dir[n];
		pos[0]=Op->Exc->Curr_index[0][n];
		pos[1]=Op->Exc->Curr_index[1][n];
		pos[2]=Op->Exc->Curr_index[2][n];
		SetCurr(ny,pos, GetCurr(ny,pos) + Op->Exc->Curr_amp[n]*Op->Exc->Signal_curr[exc_pos]);
	}

	// write the current excitation function into the file "ht"
	if (numTS < Op->Exc->Length)
		file_ht << (numTS+0.5) * Op->GetTimestep() << "\t" << Op->Exc->Signal_curr[numTS] << "\n"; // do not use std::endl here, because it will do an implicit flush
}

void Engine::DoPreVoltageUpdates()
{
	for (int n=m_Eng_exts.size()-1;n>=0;--n)
		m_Eng_exts.at(n)->DoPreVoltageUpdates();

}

void Engine::DoPostVoltageUpdates()
{
	for (size_t n=0;n<m_Eng_exts.size();++n)
		m_Eng_exts.at(n)->DoPostVoltageUpdates();
}

void Engine::Apply2Voltages()
{
	for (size_t n=0;n<m_Eng_exts.size();++n)
		m_Eng_exts.at(n)->Apply2Voltages();
}

void Engine::DoPreCurrentUpdates()
{
	for (int n=m_Eng_exts.size()-1;n>=0;--n)
		m_Eng_exts.at(n)->DoPreCurrentUpdates();
}

void Engine::DoPostCurrentUpdates()
{
	for (size_t n=0;n<m_Eng_exts.size();++n)
		m_Eng_exts.at(n)->DoPostCurrentUpdates();
}

void Engine::Apply2Current()
{
	for (size_t n=0;n<m_Eng_exts.size();++n)
		m_Eng_exts.at(n)->Apply2Current();
}

bool Engine::IterateTS(unsigned int iterTS)
{
	for (unsigned int iter=0;iter<iterTS;++iter)
	{
		//voltage updates with extensions
		DoPreVoltageUpdates();
		UpdateVoltages(0,numLines[0]);
		DoPostVoltageUpdates();
		Apply2Voltages();
		ApplyVoltageExcite();

		//current updates with extensions
		DoPreCurrentUpdates();
		UpdateCurrents(0,numLines[0]-1);
		DoPostCurrentUpdates();
		Apply2Current();
		ApplyCurrentExcite();

		++numTS;
	}
	return true;
}
