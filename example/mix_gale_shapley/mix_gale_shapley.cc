#include <cstdio>
#include <cmath>
#include <sys/time.h>

#include <vector>
#include <list>
#include <algorithm>

#include <TFile.h>
#include <TTree.h>
#include <TSystem.h>
#include <iostream>

//#define HI_TREE "hiEvtAnalyzer/HiTree"
#define HI_TREE "_tree_event"
#define HI_TREE_2 "AliAnalysisTaskNTGJ/_tree_event"
namespace {

	typedef unsigned short index_t;

	size_t nevent(const char *filename)
	{
		TFile *root_file = TFile::Open(filename);

		if (root_file == NULL) {
		  fprintf(stderr, "%s:%d: ROOT FILE FAIL\n",__FILE__, __LINE__);
			return 0;
		}

		TTree *hi_tree = dynamic_cast<TTree *>(root_file->Get(HI_TREE));

		if (hi_tree == NULL) {
		  hi_tree = dynamic_cast<TTree *>(root_file->Get(HI_TREE_2));		
		  if(hi_tree == NULL){
		    fprintf(stderr, "%s:%d: TREE FAIL\n",__FILE__, __LINE__);
		    return 0;
		  }
		}

		const size_t ret = hi_tree->GetEntries();

		root_file->Close();

		return ret;
	}

	std::vector<float> feature_extract(const char *filename,
					   const size_t event_start,
					   const size_t event_end,
					   const size_t nfeature)
	{
		TFile *root_file = TFile::Open(filename);

		if (root_file == NULL) {
		  fprintf(stderr, "%s:%d: ROOT FILE FAIL\n",__FILE__, __LINE__);
			return std::vector<float>();
		}

		TTree *hi_tree = dynamic_cast<TTree *>(root_file->Get(HI_TREE));

		if (hi_tree == NULL) {
		  hi_tree = dynamic_cast<TTree *>(root_file->Get(HI_TREE_2));
		  if(hi_tree == NULL){
		    fprintf(stderr, "%s:%d: TREE FAIL\n",__FILE__, __LINE__);
		    return std::vector<float>();
		  }
		}

		double v[3];

		hi_tree->SetBranchAddress("primary_vertex", v);

		float multiplicity_v0[64];
		  //float centrality_v0m;

		switch (nfeature) {
		case 2:
			hi_tree->SetBranchAddress("multiplicity_v0", &multiplicity_v0);
			break;
		default:
			fprintf(stderr, "%s:%d: illegal nfeature = %lu\n",
					__FILE__, __LINE__, nfeature);
			return std::vector<float>();
		}

		std::vector<float> ret;

		for (size_t i = event_start; i < event_end; i++) {
			hi_tree->GetEntry(i);

			ret.push_back(v[2]);
			if (nfeature >= 2) {
 			        float multp_sum = 0;
			        for (int k = 0; k < 64; k++) {
				  multp_sum += multiplicity_v0[k];
				}
				ret.push_back(multp_sum);
			}
		}

		root_file->Close();

		return ret;
	}

	void feature_normalize(std::vector<float> &u,
						   std::vector<float> &v, const size_t n)
	{
		std::vector<float> s(n, 0);

		for (size_t j = 0; j < n; j++) {
			float s_j = 0;
#ifdef _OPENMP
#pragma omp parallel for shared(u) reduction(+: s_j)
#endif // _OPENMP
			for (size_t i = 0; i < u.size(); i += n) {
				s_j += fabsf(u[i + j]);
			}
			s[j] = s_j;
		}
		for (size_t j = 0; j < n; j++) {
			float s_j = 0;

#ifdef _OPENMP
#pragma omp parallel for shared(u) reduction(+: s_j)
#endif // _OPENMP
			for (size_t i = 0; i < v.size(); i += n) {
				s_j += fabsf(v[i + j]);
			}
			s[j] += s_j;
		}
		for (size_t j = 0; j < n; j++) {
			s[j] = (u.size() + v.size()) / s[j];

			fprintf(stderr, "%s:%d: %lu %f\n", __FILE__, __LINE__, j, s[j]);
		}

#ifdef _OPENMP
#pragma omp parallel for shared(u, s)
#endif // _OPENMP
		for (size_t i = 0; i < u.size(); i += n) {
			for (size_t j = 0; j < n; j++) {
				u[i + j] *= s[j];
			}
		}
#ifdef _OPENMP
#pragma omp parallel for shared(v, s)
#endif // _OPENMP
		for (size_t i = 0; i < v.size(); i += n) {
			for (size_t j = 0; j < n; j++) {
				v[i + j] *= s[j];
			}
		}
	}
}

bool preference_compare(const std::pair<float, index_t> u,
						const std::pair<float, index_t> v)
{
	return u.first > v.first;
}

void order_preference(std::vector<std::list<index_t> > &up,
					  std::vector<std::list<index_t> > &vp,
					  std::vector<float> u, std::vector<float> v,
					  const size_t n, const size_t nduplicate_v)
{
	const size_t u_size_n = u.size() / n;
	const size_t v_size_n = v.size() / n;

	fprintf(stderr, "%s:%d: %lux%lu\n", __FILE__, __LINE__,
			u_size_n, v_size_n);

	const size_t size_max = std::max(u_size_n, v_size_n);

	up.resize(u_size_n, std::list<index_t>());
#ifdef _OPENMP
#pragma omp parallel for shared(up)
#endif // _OPENMP
	for (size_t i = 0; i < u_size_n; i++) {
		std::vector<std::pair<float, index_t> > l;

		for (size_t j = 0; j < v_size_n; j++) {
			float d = 0;

			for (size_t k = 0; k < n; k++) {
				d += std::pow(u[i * n + k] - v[j * n + k], 2);
			}
			if (d==0) d = 999999; //Avoid pairing identical events
			l.push_back(std::pair<float, size_t>(d, j));
		}
		std::sort(l.begin(), l.end(), preference_compare);
		// up.push_back(std::list<index_t>());
		for (size_t j = 0; j < l.size(); j++) {
			for (size_t k = 0; k < nduplicate_v; k++) {
				up[i].push_front(l[j].second + k * v_size_n);
			}
		}
		up[i].resize(size_max, v_size_n);

		if (i % 100 == 0) {
			fprintf(stderr, "%s:%d: %lu/%lu\n", __FILE__, __LINE__,
					i, u_size_n);
		}
	}

	vp.resize(v_size_n * nduplicate_v, std::list<index_t>());
#ifdef _OPENMP
#pragma omp parallel for shared(vp)
#endif // _OPENMP
	for (size_t j = 0; j < v_size_n; j++) {
		std::vector<std::pair<float, index_t> > l;

		for (size_t i = 0; i < u_size_n; i++) {
			float d = 0;

			for (size_t k = 0; k < n; k++) {
				d += std::pow(u[i * n + k] - v[j * n + k], 2);
			}
			if (d==0) d = 999999;
			l.push_back(std::pair<float, index_t>(d, i));
		}
		std::sort(l.begin(), l.end(), preference_compare);

		std::list<index_t> b;

		for (size_t i = 0; i < l.size(); i++) {
			b.push_front(l[i].second);
		}
		b.resize(size_max, u_size_n);
		for (size_t k = 0; k < nduplicate_v; k++) {
			vp[j * nduplicate_v + k] = b;
		}

		if (j % 100 == 0) {
			fprintf(stderr, "%s:%d: %lu/%lu\n", __FILE__, __LINE__,
					j, v_size_n);
		}
	}

	fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, "Order Preference done for this block");
}

std::vector<index_t> gale_shapley(std::vector<std::list<index_t> > &mp,
								 std::vector<std::list<index_t> > &fp)
{
	std::vector<index_t> m_to_f_engaged(mp.size(), fp.size());
	std::vector<index_t> f_to_m_engaged(fp.size(), mp.size());

	std::vector<std::vector<std::pair<
		std::vector<std::list<index_t> >::iterator,
								std::list<index_t>::iterator> > >

	mp_index;

	mp_index.resize(fp.size());
	for (std::vector<std::list<index_t> >::iterator
			 iterator_outer = mp.begin();
		 iterator_outer != mp.end(); iterator_outer++) {
		for (std::list<index_t>::iterator
				 iterator_inner = iterator_outer->begin();
			 iterator_inner != iterator_outer->end();
			 iterator_inner++) {
			mp_index[*iterator_inner].push_back(
				std::pair<std::vector<std::list<index_t> >::iterator,
				std::list<index_t>::iterator>(
					iterator_outer, iterator_inner));
			
		}

		if ((iterator_outer - mp.begin()) % 100 == 0) {
		  fprintf(stderr, "%s:%d: %lu/%lu\n", __FILE__, __LINE__,
			  iterator_outer - mp.begin(), mp.size());
		}
	}

	for (;;) {
		std::vector<index_t>::const_iterator m_iterator =
			std::find(m_to_f_engaged.begin(),
					  m_to_f_engaged.end(), fp.size());

		if (m_iterator == m_to_f_engaged.end()) {
			break;
		}

		const index_t m = m_iterator - m_to_f_engaged.begin();
		const index_t w = mp[m].front();

		if (m % 100 == 0 || w % 100 == 0) {
			fprintf(stderr, "%s:%d: %hu<>%hu\n", __FILE__, __LINE__,
					m, w);
		}

		// Some man p is engaged to w
		index_t p = f_to_m_engaged[w];

		if (p != mp.size()) {
			// Assign p to be free
			m_to_f_engaged[p] = fp.size();
		}
		f_to_m_engaged[w] = m;
		m_to_f_engaged[m] = w;

		std::list<index_t>::iterator s =
			std::find(fp[w].begin(), fp[w].end(), m);

		s++;
		for (std::vector<std::pair<std::vector<std::list<index_t> >::
				 iterator, std::list<index_t>::iterator> >::iterator
				 iterator = mp_index[w].begin();
			 iterator != mp_index[w].end(); iterator++) {
			iterator->first->erase(iterator->second);
		}
		fp[w].erase(s, fp[w].end());
	}
	return m_to_f_engaged;
}

void mix_gale_shapley(const char *filename_0, const char *filename_1, const char *mixing_start, const char *mixing_end,
					  const int nfeature, const int nduplicate)
{
        size_t mix_start = atoi(mixing_start);
	size_t mix_end = atoi(mixing_end);

	const size_t nevent_0 = nevent(filename_0);
	const size_t nevent_1 = nevent(filename_1);

<<<<<<< HEAD
	const size_t block_size_max = 1000; //FIXME: May need reducing,
=======
	const size_t block_size_max = 2000; //FIXME: May need reducing,
>>>>>>> eed1d90c226e1a6035acb9dcd3626dbdc670d08d
	const size_t nblocks = std::min(nevent_0, nevent_1 * nduplicate) /
		block_size_max + 1;

	//const size_t nblock = nblocks - 1; // FIXME:Use % and rounding to get all events 
	size_t nblock = nblocks - 1; // FIXME:Use % and rounding to get all events 
	const size_t nblock_1 = (std::max(nevent_0,nevent_1 * nduplicate)/
		block_size_max + 1)-1;

	//size_t lmin,lmax; 

	size_t width = 100; //if changed, also must change when writing to Tree
	const size_t n_mix = 200;


	std::vector<std::vector<Long64_t> > Matches;
	fprintf(stderr,"\n%s\n","Using Assymetric File block distribution, LARGER FILE: SECEND ARG"); 

	for(size_t h = 0; h < nblock+1; h++){
	  const size_t event_start_0 = h * nevent_0 / (nblock + 1);
	  const size_t event_end_0 = (h + 1) * nevent_0 / (nblock + 1);
	  const size_t nevents_0 = event_end_0 - event_start_0;	  
	    std::vector<std::vector<Long64_t> > k;		  
	  
	    std::vector<float> feature_0 =
	      feature_extract(filename_0, event_start_0, event_end_0,nfeature);
	    
	    fprintf(stderr,"%s %lu %s %lu\n","Block",h,"of",nblock);
	    
	    // //foregoing this block structure as number of events to be mixed approaches nblocks	    

	    // if (h < width) {
	    //   lmin = 0; 
	    //   lmax = 2*width+1;
	    // }

	    // else if (h+width > nblock_1) {
	    //   lmin = nblock_1-2*width; 
	    //   lmax = nblock_1+1;
	    //   if(h+width > nblock_1){
	    // 	lmin = 0;
	    // 	lmax = 2*width+1;
	    //   }
	    // }

	    // else {
	    //   lmin = h-width;  	 
	    //   lmax = h+width+1;
	    // }
	    // for (size_t i = lmin+mix_start; i < lmin+mix_end+1; i++) {

	    //size_t imax = std::min(n_mix,nblock_1);
	    //for (size_t i = h; i < h+n_mix+1; i++) {
	    //for (size_t i = 0; i < n_mix; i++) {
	    
	    for (size_t i = mix_start; i < mix_end+1; i++) {
	      //if (std::strncmp(filename_0,filename_1,strlen(filename_0))==0 && i==h) continue;
	    
	      //for assymtric files:
	      size_t event_start_1 = i * nevent_1 / (nblock_1+1);
	      size_t event_end_1 = event_start_1 + (event_end_0 - event_start_0);
	      const size_t nevents_1 = event_end_1 - event_start_1; //might fail if remainder is < 2000....
	      if (event_end_1 > nevent_1) {
		fprintf(stderr,"%s:%d: event_end out of bounds",__FILE__, __LINE__);
		continue;
		  }

	      fprintf(stderr,"%s:%d:%s %lu %s %lu: %s %zu %s %lu, Event in larger NTuple: %zu \n\n",
<<<<<<< HEAD
		      __FILE__, __LINE__,"Block",h,"of",nblock, "Mixed Event",i-mix_start, "of",mix_end-mix_start, event_start_1);
=======
		      __FILE__, __LINE__,"Block",h,"of",nblock, "Mixed Event",i-h+1, "of",imax, event_start_1);
>>>>>>> eed1d90c226e1a6035acb9dcd3626dbdc670d08d

	      //if(nevents_1<nevents_0) event_end_1 += (nevents_0-nevents_1);
	      //FIXME:small # events mix with themselves once. need conditional using last block
	      
		std::vector<float> feature_1 =
			feature_extract(filename_1, event_start_1, event_end_1,
							nfeature);

		{
			std::vector<float> feature_0_scaled = feature_0;
			std::vector<float> feature_1_scaled = feature_1;
			
			feature_normalize(feature_0_scaled, feature_1_scaled,
							  nfeature);

			std::vector<std::list<index_t> > preference_0;
			std::vector<std::list<index_t> > preference_1;

			order_preference(preference_0, preference_1,
							 feature_0_scaled, feature_1_scaled,
							 nfeature, nduplicate);

			std::vector <index_t> m;
			m = gale_shapley(preference_0, preference_1);

			const size_t feature_1_size_nfeature =
			  feature_1.size() / nfeature;

			std::vector <Long64_t>tempflat;

			for (size_t j = 0; j < m.size(); j++) {                                        
			  Long64_t q = event_start_1 + (m[j] % feature_1_size_nfeature);
			  tempflat.push_back(q);
			}			
			k.push_back(tempflat);
		}
	  } //i
	  for (size_t j = 0; j < k[0].size(); j++){
	    std::vector <Long64_t> P;
<<<<<<< HEAD
	    P.push_back(event_start_0+j); //taken out for now such that merging does not constantly contain same event.
=======
	    //    P.push_back(event_start_0+j); //taken out for now such that merging does not constantly contain same event.
>>>>>>> eed1d90c226e1a6035acb9dcd3626dbdc670d08d
	    for (size_t l = 0; l < k.size(); l++){
	      P.push_back(k[l][j]);
	    }
	    Matches.push_back(P);
	  }

	}//h

	  //    write to txt
<<<<<<< HEAD
	FILE * txtfile = fopen (Form("v2_6GeVpairs%lu_%lu.txt",mix_start,mix_end),"w");
	  for (size_t t=0; t<Matches.size();t++){
	    for (size_t s=0; s<Matches[t].size();s++){
	      fprintf(txtfile, "%lld ", Matches[t][s]);
	    }
	    fprintf(txtfile, "%s\n","");
	  }
	 fclose (txtfile);
=======
// 	  FILE * txtfile = fopen ("pairs.txt","w");
// 	  for (size_t t=0; t<Matches.size();t++){
// 	    for (size_t s=1; s<Matches[t].size();s++){
// 	      fprintf(txtfile, "%lld ", Matches[t][s]);
// 	    }
// 	    fprintf(txtfile, "%s\n","");
// 	  }
// 	  fclose (txtfile);
>>>>>>> eed1d90c226e1a6035acb9dcd3626dbdc670d08d
	  

	// write to TTree
	//if (strcmp(filename_0,filename_1) == 0){

	  TFile *root_file = new TFile(filename_0,"update");
	  TTree *hi_tree = dynamic_cast<TTree *>(root_file->Get(HI_TREE));
<<<<<<< HEAD
	  TFile *newfile = new TFile(Form("13defv1_c_%lu_%lu_6GeV_TrackSkim_mixed.root",mix_start,mix_end),"recreate");	  
=======
	  TFile *newfile = new TFile("13def_c_4GeVTrackSkim_mixed_root","recreate");	  
>>>>>>> eed1d90c226e1a6035acb9dcd3626dbdc670d08d

	  //TFiles are more closely associated with TTrees, need to clone, not append....
	  TTree *newtree = hi_tree->CloneTree(0);

	  unsigned int n_mix_events = 2*width;
	  ULong64_t nentries = hi_tree->GetEntries();    
	  Long64_t Mix_Events[n_mix_events];

	  fprintf(stderr, "%llu\n",nentries);
	  
	  TBranch *MixE = newtree->Branch("Mix_Events", Mix_Events, "&Mix_Events[200]/L");
	  
	  for (ULong64_t t = 0; t<nentries;t++){
	    hi_tree->GetEntry(t);
	    
	    if(t < Matches.size()){
<<<<<<< HEAD
	      for (size_t s=1; s<(Matches[t]).size();s++){ //s=1 for same event start. s=0 when taken out
=======
	      for (size_t s=0; s<(Matches[t]).size();s++){ //s=1 for same event start. s=0 when taken out
>>>>>>> eed1d90c226e1a6035acb9dcd3626dbdc670d08d
		Mix_Events[s-1]=Matches[t][s]; 
		fprintf(stderr, "%llu:%lld\n", t,Mix_Events[s-1]);
	      }
	    }
	    
	    else if (t >= Matches.size()){
	      for(size_t u = 0; u<n_mix_events; u++){
		//Mix_Events[u] = t; //Fill with own event number. Skip During correlation function
		Mix_Events[u] = 999999999;
		fprintf(stderr, "%llu:%lld\n",t,Mix_Events[u]);
	      }
	    }
	    
	    fprintf(stderr, "%s\n","");
	    newtree->Fill();  
	    
	  }//End loop over entries
	  newtree->Write();

	  delete root_file;
	  delete newfile;
	  //}
	  //else fprintf(stderr, "%s\n","Nothing written to root file.");
	
	gSystem->Exit(0);
}

int main(int argc, char *argv[])
{
	if (argc < 5) {
	  fprintf(stderr,"%s\n","Argument Syntax is [Command] [File] [File] [mix start] [mix end]");
		return EXIT_FAILURE;
	}
	mix_gale_shapley(argv[1], argv[2], argv[3], argv[4], 2, 1);
}
