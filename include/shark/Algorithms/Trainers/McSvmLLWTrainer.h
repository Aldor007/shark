//===========================================================================
/*!
 *  \brief Trainer for the Multi-class Support Vector Machine by Lee, Lin, and Wahba
 *
 *
 *  \author  T. Glasmachers
 *
 *
 *  <BR><HR>
 *  This file is part of Shark. This library is free software;
 *  you can redistribute it and/or modify it under the terms of the
 *  GNU General Public License as published by the Free Software
 *  Foundation; either version 3, or (at your option) any later version.
 *
 *  This library is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this library; if not, see <http://www.gnu.org/licenses/>.
 *
 */
//===========================================================================


#ifndef SHARK_ALGORITHMS_MCSVMLLWTRAINER_H
#define SHARK_ALGORITHMS_MCSVMLLWTRAINER_H


#include <shark/Algorithms/Trainers/AbstractSvmTrainer.h>
#include <shark/Algorithms/QP/QpMcBoxDecomp.h>
#include <shark/Algorithms/QP/QpMcLinear.h>


namespace shark {


///
/// \brief Training of the multi-category SVM by Lee, Lin and Wahba (LLW).
///
/// This is a special support vector machine variant for
/// classification of more than two classes. Given are data
/// tuples \f$ (x_i, y_i) \f$ with x-component denoting input
/// and y-component denoting the label 1, ..., d (see the tutorial on
/// label conventions; the implementation uses values 0 to d-1),
/// a kernel function k(x, x') and a regularization
/// constant C > 0. Let H denote the kernel induced
/// reproducing kernel Hilbert space of k, and let \f$ \phi \f$
/// denote the corresponding feature map.
/// Then the SVM classifier is the function
/// \f[
///     h(x) = \arg \max (f_c(x))
/// \f]
/// \f[
///     f_c(x) = \langle w_c, \phi(x) \rangle + b_c
/// \f]
/// \f[
///     f = (f_1, \dots, f_d)
/// \f]
/// with class-wise coefficients w_c and b_c given by the
/// (primal) optimization problem
/// \f[
///     \min \frac{1}{2} \sum_c \|w_c\|^2 + C \sum_i L(y_i, f(x_i))
/// \f]
/// \f[
///     \text{s.t. } \sum_c f_c = 0
/// \f]
/// The special property of the so-called LLW-machine is its
/// loss function, which arises from the application of the
/// discriminative sum operator to absolute margin violations.
/// Let \f$ h(m) = \max\{0, 1-m\} \f$ denote the hinge loss
/// as a function of the margin m, then the LLW loss is given
/// by
/// \f[
///     L(y, f(x)) = \sum_{c \not= y} h(-f_c(x))
/// \f]
///
/// For more details refer to the paper:<br/>
/// <p>Multicategory Support Vector Machines: Theory and Application to the Classification of Microarray %Data and Satellite Radiance %Data. Y. Lee, Y. Lin, and G. Wahba. Journal of the American Statistical Association 99(465), 2004.</p>
///
template <class InputType, class CacheType = float>
class McSvmLLWTrainer : public AbstractSvmTrainer<InputType, unsigned int>
{
public:

	/// \brief Convenience typedefs:
	/// this and many of the below typedefs build on the class template type CacheType.
	/// Simply changing that one template parameter CacheType thus allows to flexibly
	/// switch between using float or double as type for caching the kernel values.
	/// The default is float, offering sufficient accuracy in the vast majority
	/// of cases, at a memory cost of only four bytes. However, the template
	/// parameter makes it easy to use double instead, (e.g., in case high
	/// accuracy training is needed).
	typedef CacheType QpFloatType;
	typedef KernelMatrix<InputType, QpFloatType> KernelMatrixType;
	typedef CachedMatrix< KernelMatrixType > CachedMatrixType;
	typedef PrecomputedMatrix< KernelMatrixType > PrecomputedMatrixType;

	typedef AbstractModel<InputType, RealVector> ModelType;
	typedef AbstractKernelFunction<InputType> KernelType;
	typedef AbstractSvmTrainer<InputType, unsigned int> base_type;

	//! Constructor
	//! \param  kernel         kernel function to use for training and prediction
	//! \param  C              regularization parameter - always the 'true' value of C, even when unconstrained is set
	//! \param  unconstrained  when a C-value is given via setParameter, should it be piped through the exp-function before using it in the solver?
	McSvmLLWTrainer(KernelType* kernel, double C, bool unconstrained = false)
	: base_type(kernel, C, unconstrained)
	{ }

	/// \brief From INameable: return the class name.
	std::string name() const
	{ return "McSvmLLWTrainer"; }

	void train(KernelExpansion<InputType>& svm, const LabeledData<InputType, unsigned int>& dataset)
	{
		std::size_t ic = dataset.numberOfElements();
		unsigned int classes = numberOfClasses(dataset);
		// the following test is "<=" rather than "=" to account for the rare case that one fold doesn't contain all classes due to sample scarcity
		SHARK_CHECK(classes <= svm.outputSize(), "[McSvmLLWTrainer::train] invalid number of outputs in the kernel expansion");
		svm.setKernel(base_type::m_kernel);
		svm.setBasis(dataset.inputs());
		classes = svm.outputSize();

		RealMatrix linear(ic, classes-1,1.0);
		UIntVector rho(classes-1);
		{
			for (unsigned int p=0; p<classes-1; p++) 
				rho(p) = p;
		}
		QpSparseArray<QpFloatType> nu(classes * (classes-1), classes, classes*(classes-1));
		{
			unsigned int y, p, pp, r;
			for (r=0, y=0; y<classes; y++)
			{
				for (p=0, pp=0; p<classes-1; p++, pp++, r++)
				{
					if (pp == y) pp++;
					nu.add(r, pp, (QpFloatType)-1.0);
				}
			}
		}
		QpSparseArray<QpFloatType> M(classes * (classes-1) * classes, classes-1, classes * (classes-1) * (classes-1));
		{
			unsigned int r;
			unsigned int yv, pv, ppv;
			unsigned int yw, pw;
			QpFloatType mood = (QpFloatType)(-1.0 / (double)classes);
			QpFloatType val = (QpFloatType)1.0 + mood;
			for (r=0, yv=0; yv<classes; yv++)
			{
				for (pv=0, ppv=0; pv<classes-1; pv++, ppv++)
				{
					if (ppv == yv) ppv++;
					for (yw=0; yw<classes; yw++, r++)
					{
						M.setDefaultValue(r, mood);
						if (ppv != yw)
						{
							pw = ppv - (ppv > yw ? 1 : 0);
							M.add(r, pw, val);
						}
					}
				}
			}
		}
		
		// solve the problem
		RealMatrix alpha(ic,classes-1);
		RealVector bias(classes,0);
		KernelMatrixType km(*base_type::m_kernel, dataset.inputs());
		if (base_type::precomputeKernel())
		{
			PrecomputedMatrixType matrix(&km);
			QpMcBoxDecomp< PrecomputedMatrixType > problem(matrix, M, dataset.labels(), linear, this->C());
			QpSolutionProperties& prop = base_type::m_solutionproperties;
			problem.setShrinking(base_type::m_shrinking);
			if(svm.hasOffset()){
				BiasSolver<  PrecomputedMatrixType > biasSolver(&problem);
				biasSolver.solve(bias,base_type::m_stoppingcondition,nu);
			}
			else{
				QpSolver<QpMcBoxDecomp< PrecomputedMatrixType > > solver(problem);
				solver.solve( base_type::m_stoppingcondition, &prop);
			}
			alpha = problem.solution();
		}
		else
		{
			CachedMatrixType matrix(&km, base_type::m_cacheSize);
			QpMcBoxDecomp< CachedMatrixType> problem(matrix, M, dataset.labels(), linear, this->C());
			QpSolutionProperties& prop = base_type::m_solutionproperties;
			problem.setShrinking(base_type::m_shrinking);
			if(svm.hasOffset()){
				BiasSolver<CachedMatrixType> biasSolver(&problem);
				biasSolver.solve(bias,base_type::m_stoppingcondition,nu);
			}
			else{
				QpSolver<QpMcBoxDecomp< CachedMatrixType> > solver(problem);
				solver.solve( base_type::m_stoppingcondition, &prop);
			}
			alpha = problem.solution();
		}

		// write the solution into the model
		for (std::size_t i=0; i<ic; i++)
		{
			unsigned int y = dataset.element(i).label;
			for (std::size_t c=0; c<classes; c++)
			{
				double sum = 0.0;
				unsigned int r = (classes-1) * y;
				for (std::size_t p=0; p<classes-1; p++, r++) 
					sum += nu(r, c) * alpha(i,p);
				svm.alpha(i,c) = sum;
			}
		}
		if (svm.hasOffset()) svm.offset() = bias;

		base_type::m_accessCount = km.getAccessCount();
		if (base_type::sparsify()) svm.sparsify();
	}
};


template <class InputType>
class LinearMcSvmLLWTrainer : public AbstractLinearSvmTrainer<InputType>
{
public:
	typedef AbstractLinearSvmTrainer<InputType> base_type;

	LinearMcSvmLLWTrainer(double C, bool unconstrained = false)
	: AbstractLinearSvmTrainer<InputType>(C, unconstrained){ }

	/// \brief From INameable: return the class name.
	std::string name() const
	{ return "LinearMcSvmLLWTrainer"; }

	void train(LinearClassifier<InputType>& model, const LabeledData<InputType, unsigned int>& dataset)
	{
		std::size_t dim = inputDimension(dataset);
		std::size_t classes = numberOfClasses(dataset);
/*
		CompressedRealMatrix w(classes, dim);
		std::vector<CompressedRealMatrixRow> w_s;
		for (std::size_t c=0; c<classes; c++) w_s.push_back(CompressedRealMatrixRow(w, c));
		typedef McPegasos<CompressedRealVector> PegasosType;
		PegasosType::solve(
				dataset,
				PegasosType::emAbsolute,
				PegasosType::elDiscriminativeSum,
				true,
				C(),
				w_s,
				std::min((std::size_t)1000, dataset.numberOfElements()),
				accuracy());
*/
		QpMcLinearLLW<InputType> solver(dataset, dim, classes);
		RealMatrix w = solver.solve(this->C(), this->stoppingCondition(), &this->solutionProperties(), this->verbosity() > 0);
		model.decisionFunction().setStructure(w);
	}
};


// shorthands for unified naming scheme; we resort to #define
// statements since old c++ does not support templated typedefs
#define McSvmADSTrainer McSvmLLWTrainer
#define LinearMcSvmADSTrainer LinearMcSvmLLWTrainer


}
#endif
