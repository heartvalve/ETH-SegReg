#pragma once
#include "BaseMRF.h"
#include "Log.h"
#include "GCoptimization.h"
#include <vector>
#include <google/heap-profiler.h>
#include "ordering.cpp"
#include <limits.h>
//#include "malloc.c"
using namespace std;




template<class Solver>
class SmoothCostFunctor {
private:
    typename Solver::SmoothFn function;
public:
    //void SetSolver(Solver * clas){cls=clas;}
    void SetFunction(typename Solver::SmoothFn f){function=f;}
    virtual float compute(int s1, int s2, int l1, int l2){
        return function(s1,s2,l1,l2);
    }
};

template<class TGraphModel>
class GCO_SRSMRFSolver :public BaseMRFSolver<TGraphModel>{
public:

    typedef GCO_SRSMRFSolver<TGraphModel> Self;
    typedef TGraphModel GraphModelType;
	typedef typename GraphModelType::Pointer GraphModelPointerType;

    static const int D = GraphModelType::ImageType::ImageDimension;
    typedef GCoptimizationGeneralGraph MRFType;

    typedef SmoothCostFunctor<Self> Functor;
    typedef float (* SmoothFn)(int s1,int s2,int l1, int l2);
protected:
    MRFType * m_optimizer;
	double m_unarySegmentationWeight,m_pairwiseSegmentationWeight;
	double m_unaryRegistrationWeight,m_pairwiseRegistrationWeight;
	static double m_pairwiseSegmentationRegistrationWeight;
	int verbose;
    
    int nNodes, nRegNodes, nSegNodes, nEdges;
    clock_t m_start;
    int nRegLabels;
    int nSegLabels;
    bool m_segmented, m_registered;
    double m_lastLowerBound;
    Functor * smoothCostFunctor;


    //ugly globals instead of ugly static members because of GCO
    static vector<vector<vector<map<int,float> > > > (*regPairwise);//,(*segPairwise);//(*srsPairwise);
    static vector<vector<vector<float > > > *srsPairwise;
    static vector<vector<vector<vector<float> > > > *segPairwise;
    static int S0,S1;
    static int GLOBALnRegNodes,GLOBALnSegNodes,GLOBALnSegLabels,GLOBALnRegLabels;
    static GraphModelPointerType m_GraphModel;
    //returns the neighbor direction. 
    //idx1= d0 + d1*S0 + d2*S0*S1 
    //idx2 is either
    //1 d0+1 +  d1   *S0 +  d2   *S0*S1
    //2 d0   + (d1+1)*S0 +  d2   *S0*S1
    //3 d0   +  d1   *S0 + (d2+1)*S0*S1
    static int getRelativeNodeIndex(int idx1,int idx2, int S0, int S1=-1 ){
        int diff =  idx2-idx1;
        if (diff == 1	)		return 0;
        if (diff == S0	)	return 1;
        if (diff ==	S0*S1)	return 2;	
        std::cerr<<"Error! idx difference doesn make sense : "<<VAR(diff)<<std::endl;
        return -1;
    }
    //#define CACHESRS
public:
    static float GLOBALsmoothFunction(int node1, int node2, int label1, int label2){
        float pot=-1;
        if (node1>node2){
            int tmp=node1;        node1=node2; node2=tmp;
            tmp=label1; label1=label2; label2=tmp;
        }
        if (node1>=GLOBALnRegNodes && node2>=GLOBALnRegNodes){
            //segmentation pairwise!
            if ( (label1<GLOBALnRegLabels) || (label2<GLOBALnRegLabels) ){
                pot=0.0;
            }else{
                pot=(*segPairwise)[label1-GLOBALnRegLabels][label2-GLOBALnRegLabels][node1-GLOBALnRegNodes][getRelativeNodeIndex(node1-GLOBALnRegNodes,node2-GLOBALnRegNodes,S0,S1)];
            }
        }else if (node1<GLOBALnRegNodes && node2<GLOBALnRegNodes){
            //registration pairwise!
            if (label2>=GLOBALnRegLabels || label1>=GLOBALnRegLabels){
                pot=0.0;
            }else{
                pot=(*regPairwise)[label1][label2][node1][node2];
            }
        }else{
            //srs pairwise
            if (label2<GLOBALnRegLabels || label1>=GLOBALnRegLabels){
                //impossible labelling, either regnode getting assigned a seglabel, or vice versa
                pot=100000;
            }else {
#ifdef CACHESRS
                pot=(*srsPairwise)[label2-GLOBALnRegLabels][label1][node2-GLOBALnRegNodes];

#else
                //           this->m_GraphModel->getPairwiseRegSegPotential(segRegNeighbors[i],d,l2,l1);
                pot=m_pairwiseSegmentationRegistrationWeight*m_GraphModel->getPairwiseRegSegPotential(node2-GLOBALnRegNodes,label1,label2-GLOBALnRegLabels);
                //LOG<<VAR(pot)<<" "<<VAR(node2-GLOBALnRegNodes)<<" "<<VAR(label1)<<" "<<VAR(label2-GLOBALnRegLabels)<<endl;

#endif
                
            }
        }
        //LOGV(25)<<VAR(pot)<<" "<<VAR(node1)<<" "<<VAR(label1)<<" " <<VAR(node2)<<" "<<VAR(label2)<<endl;
        return pot;
    }

public:
    GCO_SRSMRFSolver(GraphModelPointerType  graphModel,
                     double unaryRegWeight=1.0, 
                     double pairwiseRegWeight=1.0, 
                     double unarySegWeight=1.0, 
                     double pairwiseSegWeight=1.0, 
                     double pairwiseSegRegWeight=1.0,
                     int vverbose=false)
        
	{
        m_GraphModel=graphModel;
		verbose=vverbose;
		m_unarySegmentationWeight=unarySegWeight;
		m_pairwiseSegmentationWeight=pairwiseSegWeight;
        m_unaryRegistrationWeight=unaryRegWeight;
        m_pairwiseRegistrationWeight=pairwiseRegWeight;
        m_pairwiseSegmentationRegistrationWeight=pairwiseSegRegWeight;
        //createGraph();
        //init();
        m_optimizer=NULL;

      
      
    }
    GCO_SRSMRFSolver()  {
        
    }
	~GCO_SRSMRFSolver()
    {
        delete regPairwise;
        delete segPairwise;
        delete srsPairwise;
        delete m_optimizer;

    }
	virtual void createGraph(){
        clock_t start = clock();
        {
            m_segmented=false; 
            m_registered=false;
            m_segmented=0;
            m_registered=0;
        }
        LOGV(1)<<"starting graph init"<<std::endl;
        this->m_GraphModel->Init();
        clock_t endUnary = clock();
        double t1 = (float) ((double)(endUnary - start) / CLOCKS_PER_SEC);
        tUnary+=t1;       

		nNodes=this->m_GraphModel->nNodes();
        nEdges=this->m_GraphModel->nEdges();
        nRegNodes=this->m_GraphModel->nRegNodes();
        nSegNodes=this->m_GraphModel->nSegNodes();

        m_start=start;

        int edgeCount=0;
        
        nRegLabels=this->m_GraphModel->nRegLabels();
        nSegLabels=this->m_GraphModel->nSegLabels();
        m_registered=((m_pairwiseSegmentationRegistrationWeight>0 || m_unaryRegistrationWeight>0 || m_pairwiseRegistrationWeight>0) && nRegLabels);
        m_segmented=(m_pairwiseSegmentationRegistrationWeight>0 || m_unarySegmentationWeight>0 || m_pairwiseSegmentationWeight) && nSegLabels ;
        GLOBALnRegNodes= m_registered*nRegNodes;
        GLOBALnSegNodes= m_segmented*nSegNodes;
        GLOBALnRegLabels=m_registered*nRegLabels;
        GLOBALnSegLabels=m_segmented*nSegLabels;
        LOGV(5)<<VAR(GLOBALnRegNodes)<<" "<<VAR(GLOBALnRegLabels)<<" "<<VAR(GLOBALnSegNodes)<<" "<<VAR(GLOBALnSegLabels)<<endl;
        
        if (m_optimizer) delete m_optimizer;
        m_optimizer= new MRFType(GLOBALnSegNodes+GLOBALnRegNodes,GLOBALnRegLabels+GLOBALnSegLabels);
		
		//set global size variables :(
		{
			S0=this->m_GraphModel->getImageSize()[0];
			S1=this->m_GraphModel->getImageSize()[1];
		}

        logSetStage("Potential Functions");
		//		traverse grid
        if ( m_registered){
            //RegUnaries
            clock_t startUnary = clock();
            
            //now compute&set all potentials
            if (m_unaryRegistrationWeight>0){
                for (int l1=0;l1<nRegLabels;++l1)
                    {
                        GCoptimization::SparseDataCost costs[nRegNodes];
                        this->m_GraphModel->cacheRegistrationPotentials(l1);
                        for (int d=0;d<nRegNodes;++d){
                            costs[d].site=d;
                            costs[d].cost=m_unaryRegistrationWeight*this->m_GraphModel->getUnaryRegistrationPotential(d,l1);
                        }
                        m_optimizer->setDataCost(l1,costs,nRegNodes);
                    }
            }

         
            clock_t endUnary = clock();
            double t = (float) ((double)(endUnary - startUnary) / CLOCKS_PER_SEC);
            LOGV(1)<<"Registration Unaries took "<<t<<" seconds."<<endl;
            tUnary+=t;
            // Pairwise potentials
            //if (regPairwise!=NULL) delete regPairwise;
            regPairwise= new vector<vector<vector<map<int,float> > > > (nRegLabels,vector<vector<map<int,float> > >(nRegLabels,vector<map<int,float> > (nRegNodes) ) );
            for (int d=0;d<nRegNodes;++d){
                m_optimizer->setLabel(d,GLOBALnRegLabels/2);

                {//pure Registration
                    std::vector<int> neighbours= this->m_GraphModel->getForwardRegistrationNeighbours(d);
                    int nNeighbours=neighbours.size();
                    for (int i=0;i<nNeighbours;++i){
                        //LOG<<d<<" "<<regNodes[d]<<" "<<i<<" "<<neighbours[i]<<std::endl;
                        m_optimizer->setNeighbors(d,neighbours[i],1);
                        for (int l1=0;l1<nRegLabels;++l1){
                            for (int l2=0;l2<nRegLabels;++l2){                                
                                if (m_pairwiseRegistrationWeight>0)
                                    (*regPairwise)[l1][l2][d][neighbours[i]] = m_pairwiseRegistrationWeight*this->m_GraphModel->getPairwiseRegistrationPotential(d,neighbours[i],l1,l2);
                                else
                                    (*regPairwise)[l1][l2][d][neighbours[i]] = 0.0;
                            }
                        }

                        edgeCount++;
                    }
                
                }
            }
            clock_t endPairwise = clock();
         
            t = (float) ((double)(endPairwise-endUnary ) / CLOCKS_PER_SEC);
            LOGV(1)<<"Registration pairwise took "<<t<<" seconds."<<endl;

            tPairwise+=t;
        }
        if (m_segmented){
            //SegUnaries
            clock_t startUnary = clock();
            for (int l1=0;l1<nSegLabels;++l1)
                {

                    LOGV(4)<<"Allocating seg unaries for label "<<l1<<", using "<<1.0*nSegNodes*sizeof( GCoptimization::SparseDataCost ) /(1024*1024)<<" mb memory"<<std::endl;
		    
                    std::vector<GCoptimization::SparseDataCost> costas(nSegNodes);
                    //GCoptimization::SparseDataCost costas[nSegNodes];
                    LOGV(4)<<"did it :O" <<endl;
                    //GCoptimization::SparseDataCost costs[nSegNodes];
                    for (int d=0;d<nSegNodes;++d){
                        costas[d].cost=m_unarySegmentationWeight*this->m_GraphModel->getUnarySegmentationPotential(d,l1);
                        costas[d].site=d+GLOBALnRegNodes;
                    }
                    m_optimizer->setDataCost(l1+GLOBALnRegLabels,&costas[0],GLOBALnSegNodes);
                }
          
            clock_t endUnary = clock();
            double t = (float) ((double)(endUnary - startUnary) / CLOCKS_PER_SEC);
            LOGV(1)<<"Segmentation Unaries took "<<t<<" seconds."<<endl;
            LOGV(1)<<"Approximate size of seg unaries: "<<1.0/(1024*1024)*nSegNodes*nSegLabels*sizeof(double)<<" mb."<<endl;

            int nSegEdges=0,nSegRegEdges=0;
            //Segmentation smoothness cache
            //LOGV(4)<<"Allocation seg pairwise, rough theroetical size :"<<GLOBALnSegLabels*GLOBALnSegLabels*GLOBALnSegNodes*sizeof(map<int,float>)*D*(sizeof(int)+sizeof(float))/(1024*1024)<<"Mb"<<std::endl;
            //LOGV(4)<<"Allocation seg pairwise, rough theroetical size after reararanging"<<GLOBALnSegNodes*sizeof(map<int,float>)*( D*(sizeof(int)+GLOBALnSegLabels*GLOBALnSegLabels*sizeof(float)))/(1024*1024)<<"Mb"<<std::endl;
            //segPairwise= new vector<vector<vector<map<int,float> > > > (GLOBALnSegLabels,vector<vector<map<int,float> > >(GLOBALnSegLabels,vector<map<int,float> > (GLOBALnSegNodes) ) );
            //if (segPairwise!=NULL) delete segPairwise;
            segPairwise= new vector<vector<vector<vector<float> > > > (GLOBALnSegLabels,vector<vector<vector<float> > >(GLOBALnSegLabels,vector< vector<float> > (GLOBALnSegNodes,vector<float> (D)) ) );
            //LOGV(4)<<"Allocated, "<<VAR(segPairwise)<<endl;
            
            //SRS potential cache
            //srsPairwise= new vector<vector<vector<map<int,float> > > > (GLOBALnSegLabels,vector<vector<map<int,float> > >(GLOBALnRegLabels,vector<map<int,float> > (GLOBALnSegNodes) ) );
            //if (srsPairwise!=NULL) delete srsPairwise;
#ifdef CACHESRS
            srsPairwise= new vector<vector<vector<float > > > (GLOBALnSegLabels,vector<vector<float > >(GLOBALnRegLabels,vector<float>(GLOBALnSegNodes) ) );
#endif
            for (int d=0;d<nSegNodes;++d){   
                m_optimizer->setLabel(d+GLOBALnRegNodes,0+GLOBALnRegLabels);
                //pure Segmentation
                std::vector<int> neighbours= this->m_GraphModel->getForwardSegmentationNeighbours(d);
                int nNeighbours=neighbours.size();
                for (int i=0;i<nNeighbours;++i){
                    nSegEdges++;
                    m_optimizer->setNeighbors(d+GLOBALnRegNodes,neighbours[i]+GLOBALnRegNodes,1);
                    edgeCount++;
                    for (int l1=0;l1<nSegLabels;++l1){
                        for (int l2=0;l2<nSegLabels;++l2){
                            LOGV(25)<<VAR(d)<<" "<<VAR(l1)<<" "<<VAR(neighbours[i])<<" "<<l2<<endl;
                            if (m_pairwiseSegmentationWeight>0){
                                (*segPairwise)[l1][l2][d][i] = m_pairwiseSegmentationWeight*this->m_GraphModel->getPairwiseSegmentationPotential(d,neighbours[i],l1,l2);
                            }else{
                                (*segPairwise)[l1][l2][d][i] = 0.0;
                            }
                        }
                    }
                    
                }
                if (m_registered){
                    std::vector<int> segRegNeighbors=this->m_GraphModel->getSegRegNeighbors(d);
                    nNeighbours=segRegNeighbors.size();
                    if (nNeighbours==0) {LOG<<"ERROR: node "<<d<<" seems to have no neighbors."<<std::endl;}

                    for (int i=0;i<nNeighbours;++i){
                        m_optimizer->setNeighbors(d+GLOBALnRegNodes,segRegNeighbors[i],1);
                        edgeCount++;
#ifdef CACHESRS

                        nSegRegEdges++;
                        for (int l1=0;l1<nSegLabels;++l1){
                            for (int l2=0;l2<nRegLabels;++l2){
                                //forward
                                LOGV(25)<<VAR(d)<<" "<<VAR(l1)<<" "<<VAR(segRegNeighbors[i])<<" "<<VAR(l2)<<endl;
                                if (m_pairwiseSegmentationRegistrationWeight>0){
                                    (*srsPairwise)[l1][l2][d]=m_pairwiseSegmentationRegistrationWeight*this->m_GraphModel->getPairwiseRegSegPotential(segRegNeighbors[i],d,l2,l1);
                                    //(*srsPairwise)[l1][l2][d][segRegNeighbors[i]]=m_pairwiseSegmentationRegistrationWeight*this->m_GraphModel->getPairwiseRegSegPotential(segRegNeighbors[i],d,l2,l1);
                                }else{
                                    (*srsPairwise)[l1][l2][d]=0.0;
                                }
                            }
                        }
#endif

                    }

                }
            }
            clock_t endPairwise = clock();
            t = (float) ((double)(endPairwise-endUnary ) / CLOCKS_PER_SEC);
            LOGV(1)<<"Segmentation + SRS pairwise took "<<t<<" seconds."<<endl;
            LOGV(1)<<"Approximate size of seg pairwise: "<<1.0/(1024*1024)*nSegEdges*nSegLabels*nSegLabels*sizeof(double)<<" mb."<<endl;
            LOGV(1)<<"Approximate size of SRS pairwise: "<<1.0/(1024*1024)*nSegRegEdges*nSegLabels*nRegLabels*sizeof(double)<<" mb."<<endl;
            
        }
        m_optimizer->setSmoothCost(&GLOBALsmoothFunction);

        clock_t finish = clock();
        double t = (float) ((double)(finish - start) / CLOCKS_PER_SEC);
        //tInterpolation+=t;
        LOGV(1)<<"Finished init after "<<t<<" seconds"<<std::endl;
        nEdges=edgeCount;
        logResetStage;
        int order[GLOBALnRegLabels+GLOBALnSegLabels];
        for (int l=0;l<GLOBALnSegLabels;++l){
            order[l]=GLOBALnRegLabels+l;
        }
        for (int l=0;l<GLOBALnRegLabels;++l){
            order[l+GLOBALnSegLabels]=l;
        }
        m_optimizer->setLabelOrder(order,GLOBALnRegLabels+GLOBALnSegLabels);
        m_optimizer->setVerbosity(verbose>15);
    }
    

    virtual double optimize(int maxIter=20){
        clock_t opt_start=clock();
        double energy;//=m_optimizer->compute_energy();
        //LOGV(2)<<VAR(energy)<<endl;
        try{
            m_optimizer->expansion(maxIter);
            //m_optimizer->swap(maxIter);
        }catch (GCException e){
            e.Report();
        }
        energy=m_optimizer->compute_energy();
        clock_t finish = clock();
        tOpt+=((double)(finish-opt_start)/CLOCKS_PER_SEC);
        float t = (float) ((double)(finish - m_start) / CLOCKS_PER_SEC);
        LOG<<"Finished optimization after "<<t<<" , resulting energy is "<<energy<<std::endl;
        logResetStage;         
        return energy;

    }
    virtual double optimizeOneStep(int currentIter , bool & converged){
        clock_t opt_start=clock();
        double energy;//=
        //LOGV(2)<<VAR(energy)<<endl;
        try{
            m_optimizer->expansion(1);
            //m_optimizer->swap(maxIter);
        }catch (GCException e){
            e.Report();
        }
        energy=m_optimizer->compute_energy();
        clock_t finish = clock();
        tOpt+=((double)(finish-opt_start)/CLOCKS_PER_SEC);
        float t = (float) ((double)(finish - m_start) / CLOCKS_PER_SEC);
        LOG<<VAR(currentIter)<<" Finished optimization after "<<t<<" , resulting energy is "<<energy<<std::endl;
        if (currentIter>0){
            converged= (converged || (fabs(this->m_lastLowerBound-energy) < 1e-6 * this->m_lastLowerBound ));
        }
        //misuse member variable for storing last energy
        this->m_lastLowerBound=energy;
        return -1;

    }
    virtual std::vector<int> getDeformationLabels(){
        std::vector<int> Labels(GLOBALnRegNodes,0);
        if (m_registered){
            for (int i=0;i<GLOBALnRegNodes;++i){
                Labels[i]=m_optimizer->whatLabel(i);
                LOGV(20)<<"DEF "<<VAR(i)<<" "<<VAR(Labels[i])<<endl;
            }
        }
        return Labels;
    }
    virtual std::vector<int> getSegmentationLabels(){
        std::vector<int> Labels(GLOBALnSegNodes,0);
        if (m_segmented){
            for (int i=0;i<GLOBALnSegNodes;++i){
                Labels[i]=max(0,m_optimizer->whatLabel(i+GLOBALnRegNodes)-GLOBALnRegLabels);
                LOGV(20)<<"SEG "<<VAR(i)<<" "<<VAR(Labels[i])<<endl;
            }
        }
        return Labels;
    }

    void evalSolution(){
        LOG<<"NYI"<<std::endl;
    }

};

template<class T> vector<vector<vector<map<int,float> > > >  * GCO_SRSMRFSolver<T>::regPairwise = NULL;
template<class T> vector<vector<vector<vector<float> > > >   * GCO_SRSMRFSolver<T>::segPairwise = NULL;
template<class T> vector<vector<vector<float > > >  * GCO_SRSMRFSolver<T>::srsPairwise = NULL;
template<class T>  typename GCO_SRSMRFSolver<T>::GraphModelPointerType   GCO_SRSMRFSolver<T>::m_GraphModel=NULL;

template<class T> int  GCO_SRSMRFSolver<T>::S0=0;
template<class T> int  GCO_SRSMRFSolver<T>::S1=0;
template<class T> int  GCO_SRSMRFSolver<T>::GLOBALnRegNodes=0;
template<class T> int  GCO_SRSMRFSolver<T>::GLOBALnSegNodes=0;
template<class T> int  GCO_SRSMRFSolver<T>::GLOBALnRegLabels=0;
template<class T> int  GCO_SRSMRFSolver<T>::GLOBALnSegLabels=0;
template<class T>  double GCO_SRSMRFSolver<T>::m_pairwiseSegmentationRegistrationWeight=0;

// vector<vector<vector<float > > > *srsPairwise = NULL;
// vector<vector<vector<vector<float> > > > *segPairwise = NULL;
// int S0=0,S1=0;
// int GLOBALnRegNodes=0,GLOBALnSegNodes=0,GLOBALnSegLabels=0,GLOBALnRegLabels=0;  
