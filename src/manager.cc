/* -*- C++ -*-
 *
 * Copyright (c) 2014
 * Spoken Language Systems Group
 * MIT Computer Science and Artificial Intelligence Laboratory
 * Massachusetts Institute of Technology
 *
 * All Rights Reserved

./manager.cc
 *	FILE: cluster.cc 				                                *
 *										                            *
 *   				      				                            * 
 *   Chia-ying (Jackie) Lee <chiaying@csail.mit.edu>				*
 *   Feb 2014							                            *
 *  Updated by Jennifer Drexler, Feb 2015
*********************************************************************/
#include <iostream>
#include <fstream>
#include <sstream>
#include <cstdlib>
#include <cmath>

#include "manager.h"
#include "sampler.h"
#include "cluster.h"
#include "segment.h"

using namespace std;

Manager::Manager() {
}

std::vector<std::string> &split(const std::string &s, char delim, std::vector<std::string> &elems) {
  std::stringstream ss(s);
  std::string item;
  while (std::getline(ss, item, delim)) {
    elems.push_back(item);
  }
  return elems;
}

std::vector<std::string> split(const std::string &s, char delim) {
  std::vector<std::string> elems;
  split(s, delim, elems);
  return elems;
}

bool Manager::load_config(const string& fnconfig) {
  //defaults
  s_dim = 64;
  s_state = 3;
  s_dp_alpha = 1.0;
  s_beta_alpha = 5.0;
  s_beta_beta = 5.0;
  s_gamma_shape = 3.0;
  s_norm_kappa = 5.0;
  s_gamma_weight_alpha = 3.0;
  s_gamma_trans_alpha = 3.0;
  s_h0 = 0.5;

   ifstream fconfig(fnconfig.c_str(), ifstream::in);
   string line;
   while(fconfig.good()){
     fconfig >> line;
     std::vector<std::string> parts = split(line, ':');
     char *value = (char*)parts[1].c_str();
     char* nullP;
     if(parts[0] == "s_dim"){
       s_dim = std::atoi(value);
     }
     else if(parts[0] == "s_state"){
       s_state = std::atoi(value);
     }
     else if(parts[0] == "s_dp_alpha"){
       s_dp_alpha = std::strtof(value, &nullP);
     }
     else if(parts[0] == "s_beta_alpha"){
       s_beta_alpha = std::strtof(value, &nullP);
     }
     else if(parts[0] == "s_beta_beta"){
       s_beta_beta = std::strtof(value, &nullP);
     }
     else if(parts[0] == "s_gamma_shape"){
       s_gamma_shape = std::strtof(value, &nullP);
     }
     else if(parts[0] == "s_norm_kappa"){
       s_norm_kappa = std::strtof(value, &nullP);
     }
     else if(parts[0] == "s_gamma_weight_alpha"){
       s_gamma_weight_alpha = std::strtof(value, &nullP);
     }
     else if(parts[0] == "s_gamma_trans_alpha"){
       s_gamma_trans_alpha = std::strtof(value, &nullP);
     }
     else if(parts[0] == "s_h0"){
       s_h0 = std::strtof(value, &nullP);
     }
     else{
       cout << "Unrecognized config parameter: " << parts[0] << endl;
     }
   }
   return true;
}

string Manager::get_basename(string s) {
   size_t found_last_slash, found_last_period;
   found_last_slash = s.find_last_of("/");
   found_last_period = s.find_last_of(".");
   return s.substr(found_last_slash + 1, \
     found_last_period - 1 - found_last_slash);
}

bool Manager::load_bounds(const string& fnbound_list, const int g_size) {
   group_size = g_size;

   // open bounds list file
   ifstream fbound_list(fnbound_list.c_str(), ifstream::in);
   if (!fbound_list.is_open()) {
      return false;
   }
   cout << "file opened" << endl;

   int input_counter = 0;
   string indexDir;
   string dataDir; 
   while (fbound_list.good()) {
     string fn_index;
     string fn_data;
     fbound_list >> fn_index; // bounds file
     fbound_list >> fn_data; // data file

     if (fn_index != "" && fn_data != "") {
         ++input_counter;
         ifstream findex(fn_index.c_str(), ifstream::in);
         ifstream fdata(fn_data.c_str(), ifstream::binary);
         string basename = get_basename(fn_data);

         if (!findex.is_open()) {
            return false;
         }
         if (!fdata.is_open()) {
            return false;
         }

         cout << "Loading " << fn_index << "..." << endl;
         int total_frame_num;
         findex >> total_frame_num;
         cout << "total_frame_num: " << total_frame_num << endl;

         int start = 0;
         int end = 0;
         vector<Bound*> a_seg;
         while (end != total_frame_num - 1) {
	   // load one bound
	   findex >> start;
	   findex >> end;

	   int frame_num = end - start + 1;
	   float** frame_data = new float*[frame_num];

	   // read data for this bound
	   for (int i = 0; i < frame_num; ++i) {
	     frame_data[i] = new float[s_dim];
	     fdata.read(reinterpret_cast<char*>(frame_data[i]), sizeof(float) * s_dim);
	   }
	   
	   // if this is the last frame of the utterance
	   bool utt_end = false;
	   if (end == total_frame_num - 1) {
	     utt_end = true;
	   }

	   // create bound object
	   Bound* new_bound = new Bound(start, end, s_dim, utt_end);
	   new_bound -> set_index(Bound::index_counter);
	   new_bound -> set_start_frame_index(Bound::total_frames);
	   ++Bound::index_counter;
	   Bound::total_frames += end - start + 1;

	   // save data in object, then delete the temp arrays here
	   new_bound -> set_data(frame_data);
	   for(int i = 0; i < frame_num; ++i) {
	     delete[] frame_data[i];
	   }
	   delete[] frame_data;

	   // if the bound is not empty
	   if (frame_num) {
	     // add to lists (housekeeping)
	     bounds.push_back(new_bound);
	     a_seg.push_back(new_bound);

	     // sample whether this should be a boundary, from prior
	     bool phn_end = sampler.sample_boundary(new_bound);
	     new_bound -> set_phn_end(phn_end);

	     // if this is a boundary
	     if (phn_end) {
	       // create a new segment from list of bounds since the last segment
	       Segment* new_segment = new Segment(basename, a_seg);
	       vector<Bound*>::iterator iter_members = a_seg.begin();
	       for(; iter_members != a_seg.end(); ++iter_members) {
		 (*iter_members) -> set_parent(new_segment);
	       }
	       ++Segment::counter;
	       segments.push_back(new_segment);

	       // sample a cluster label for this segment
	       Cluster* new_c = sampler.sample_just_cluster(*new_segment, clusters);
	       // sample hidden states
	       sampler.sample_more_than_cluster(*new_segment, clusters, new_c);

	       new_segment -> change_hash_status(false);
	       cout << Segment::counter << " segments..." << endl;
	       cout << Cluster::counter << " clusters..." << endl;

	       //empty list of bounds
	       a_seg.clear();
	     }
	   }
	   else {
	     // if bound is empty, delete it
	     delete new_bound;
	     Bound::index_counter--;
	   }
         }

	 // we've finished reading the file - are there still bounds that we haven't added to a segment?
         if (a_seg.size()) {
	   Segment* new_segment = new Segment(basename, a_seg);
	   vector<Bound*>::iterator iter_members = a_seg.begin();
	   for(; iter_members != a_seg.end(); ++iter_members) {
	     (*iter_members) -> set_parent(new_segment);
	   }
	   ++Segment::counter;
	   segments.push_back(new_segment);

	   // sample a cluster label for this segment
	   Cluster* new_c = sampler.sample_just_cluster(*new_segment, clusters);
	   // sample hidden states
	   sampler.sample_more_than_cluster(*new_segment, clusters, new_c);

	   cout << Segment::counter << " segments..." << endl;
	   cout << Cluster::counter << " clusters..." << endl;
	   a_seg.clear();
         }

	 // get a pointer to the last bound added (i.e. the last bound in the file)
         vector<Bound*>::iterator to_last = bounds.end();
         to_last--;
         (*to_last) -> set_utt_end(true);
         (*to_last) -> set_phn_end(true);

	 // close files
         findex.close();
         fdata.close();
     }
     cout << "input_counter: " << input_counter << endl;
     
     // is this just me, or does this make no sense? -- JD
     if (!(input_counter % group_size) && fn_index != "" && fn_data != "") {
       cout << "push_back" << endl;
       batch_groups.push_back(bounds.size());
       cout << input_counter << " and " << bounds.size() << endl;
      }
     if (!(input_counter % 100)) { 
       cout << "update_clusters" << endl;
       update_clusters(false, 0);  // - JD 
       cout << "update_clusters done" << endl;
      }
   }
   // what??? -- JD
   if (input_counter % group_size) {
      batch_groups.push_back(bounds.size());
      cout << input_counter << " and " << bounds.size() << endl;
   }
   fbound_list.close();
   load_data_to_matrix(); 
   return true;
}

bool Manager::load_bounds_for_snapshot(const string& fnbound_list, const int g_size) {
   group_size = g_size;
   ifstream fbound_list(fnbound_list.c_str(), ifstream::in);
   if (!fbound_list.is_open()) {
      return false;
   }
   cout << "file opened" << endl;
   int input_counter = 0;
   string indexDir;
   string dataDir; 
   //bool readDirs = false;
   while (fbound_list.good()) {
     /*if(!readDirs){
       fbound_list >> indexDir;
       fbound_list >> dataDir;
       cout << indexDir << endl;
       cout << dataDir << endl;
       cout << "Dirs read!!" << endl;
       readDirs = true;
       }

     string fn;
     fbound_list >> fn; 
     cout << fn << endl;*/
     string fn_index;
     string fn_data;
     fbound_list >> fn_index;
     fbound_list >> fn_data;

     if (fn_index != "" && fn_data != "") {
       //if(fn != ""){
         ++input_counter;
         ifstream findex(fn_index.c_str(), ifstream::in);
	 //string fn_index = indexDir + "/" + fn + ".phn";
	 //cout << fn_index << endl;
	 //ifstream findex(fn_index.c_str(), ifstream::in);
         ifstream fdata(fn_data.c_str(), ifstream::binary);
	 //string basename = fn.substr(0, fn.find('.')); 
	 //string fn_data = dataDir + "/" + basename + "/" + fn + ".raw";
	 //cout << fn_data << endl;
	 //ifstream fdata(fn_data.c_str(), ifstream::binary);

         string basename = get_basename(fn_data);

         vector<Bound*> a_seg;
         if (!findex.is_open()) {
            return false;
         }
         if (!fdata.is_open()) {
            return false;
         }
         int start = 0;
         int end = 0;
         cout << "Loading " << fn_index << "..." << endl;
         int total_frame_num;
         findex >> total_frame_num;
         cout << "total_frame_num: " << total_frame_num << endl;
         while (end != total_frame_num - 1) {
            findex >> start;
            findex >> end;
	    cout << "start: " << start << ", end: " << end << endl;
            int frame_num = end - start + 1;
            float** frame_data = new float*[frame_num];
            for (int i = 0; i < frame_num; ++i) {
               frame_data[i] = new float[s_dim];
               fdata.read(reinterpret_cast<char*>(frame_data[i]), \
                    sizeof(float) * s_dim);
            }
            bool utt_end = false;
            if (end == total_frame_num - 1) {
               utt_end = true;
            }
            Bound* new_bound = new Bound(start, end, s_dim, utt_end);
            new_bound -> set_data(frame_data);
            new_bound -> set_index(Bound::index_counter);
            new_bound -> set_start_frame_index(Bound::total_frames);
            ++Bound::index_counter;
            Bound::total_frames += end - start + 1;
            for(int i = 0; i < frame_num; ++i) {
               delete[] frame_data[i];
            }
            delete[] frame_data;
            if (frame_num) {
               bounds.push_back(new_bound);
               a_seg.push_back(new_bound);
               // Sampler::sample_boundary(Bound*) is for sampling from prior
               //bool phn_end = sampler.sample_boundary(new_bound);
	       cout << "1" << endl;
	       vector<Bound*> new_bounds;
	       new_bounds.push_back(new_bound);
	       cout << "2" << endl;
	       bool phn_end = sampler.sample_boundary(new_bounds.begin(), segments, clusters);
	       cout << "3" << endl;

               new_bound -> set_phn_end(phn_end);
               if (phn_end) {
                  Segment* new_segment = new Segment(basename, a_seg);
                  ++Segment::counter;
                  segments.push_back(new_segment);
                  Cluster* new_c = sampler.\
                           sample_just_cluster(*new_segment, clusters);
                  sampler.sample_more_than_cluster(*new_segment, clusters, new_c);
                  vector<Bound*>::iterator iter_members = a_seg.begin();
                  for(; iter_members != a_seg.end(); ++iter_members) {
                     (*iter_members) -> set_parent(new_segment);
                  }
                  new_segment -> change_hash_status(false);
                  cout << Segment::counter << " segments..." << endl;
                  cout << Cluster::counter << " clusters..." << endl;
                  a_seg.clear();
               }
            }
            else {
               delete new_bound;
               Bound::index_counter--;
            }
         }
	 cout << "EOF" << endl;
         if (a_seg.size()) {
             cout << "Not cleaned" << endl;
             Segment* new_segment = new Segment(basename, a_seg);
             ++Segment::counter;
             segments.push_back(new_segment);
             Cluster* new_c = sampler.sample_just_cluster(*new_segment, clusters);
             sampler.sample_more_than_cluster(*new_segment, clusters, new_c);
             vector<Bound*>::iterator iter_members = a_seg.begin();
             for(; iter_members != a_seg.end(); ++iter_members) {
                (*iter_members) -> set_parent(new_segment);
             }
             cout << Segment::counter << " segments..." << endl;
             cout << Cluster::counter << " clusters..." << endl;
             a_seg.clear();
         }
         vector<Bound*>::iterator to_last = bounds.end();
         to_last--;
         (*to_last) -> set_utt_end(true);
         (*to_last) -> set_phn_end(true);
         findex.close();
         fdata.close();
	 cout << "files closed" << endl;
      }
      cout << "input_counter: " << input_counter << endl;
      if (!(input_counter % group_size) && fn_index != "" && fn_data != "") {
	//if (!(input_counter % group_size) && fn != "") {
	cout << "push_back" << endl;
         batch_groups.push_back(bounds.size());
         cout << input_counter << " and " << bounds.size() << endl;
      }
      if (!(input_counter % 100)) {
	cout << "update_clusters" << endl;
         update_clusters(false, 0);   
	cout << "update_clusters done" << endl;
      }
   }
   if (input_counter % group_size) {
      batch_groups.push_back(bounds.size());
      cout << input_counter << " and " << bounds.size() << endl;
   }
   fbound_list.close();
   load_data_to_matrix(); 
   return true;
}

void Manager::load_data_to_matrix() {
   data = new const float*[Bound::total_frames];
   list<Segment*>::iterator iter;
   int ptr = 0;
   for(iter = segments.begin(); iter != segments.end(); ++iter) { 
      int frame_num = (*iter) -> get_frame_num();
      for (int i = 0; i < frame_num; ++i) {
         data[ptr++] = (*iter) -> get_frame_i_data(i);
      }
   }
}

/*
bool Manager::load_segments(const string& fndata_list) {
   ifstream fdata_list(fndata_list.c_str(), ifstream::in);
   if (!fdata_list.is_open()) {
      return false;
   }
   while (fdata_list.good()) { 
      string fn_index;
      string fn_data;
      fdata_list >> fn_index; 
      fdata_list >> fn_data;
      if (fn_index != "" && fn_data != "") {
         ifstream findex(fn_index.c_str(), ifstream::in);
         ifstream fdata(fn_data.c_str(), ios::binary);
         string basename = get_basename(fn_data);
         if (!findex.is_open()) {
            return false;
         }
         if (!fdata.is_open()) {
            return false;
         }
         int start = 0;
         int end = 0;
         cout << "Loading " << fn_data << "..." << endl;
         while (findex.good()) {
            findex >> end;
            cout << "Loading segment " << start << "-" << end << "..." << endl; 
            int frame_num = end - start + 1;
            if (frame_num >= 3) {
               float** frame_data = new float* [frame_num];
               for (int i = 0; i < frame_num; ++i) {
                  frame_data[i] = new float[s_dim];
                  fdata.read(reinterpret_cast<char*>(frame_data[i]), \
                  sizeof(float) * s_dim);
               }
               Segment* new_segment = new Segment(frame_num, s_dim, \
                 basename, start, end);
               new_segment -> set_frame_data(frame_data);
               ++Segment::counter;
               segments.push_back(new_segment);
               sampler.sample_cluster(*new_segment, clusters);
               cout << Segment::counter << " segments..." << endl;
               cout << Cluster::counter << " clusters..." << endl;
               for (int i = 0; i < frame_num; ++i) {
                  delete[] frame_data[i];
               }
               delete[] frame_data;
               // new_segment -> show_data();
            }
            start = end + 1;
         }
         fdata.close();
         findex.close();
      }
   }
   fdata_list.close();
   return true;
}
*/
void Manager::init_sampler() {
   sampler.init_prior(s_dim, \
     s_state, \
     s_dp_alpha, \
     s_beta_alpha, s_beta_beta, \
     s_gamma_shape, \
     s_norm_kappa, \
     s_gamma_weight_alpha, \
     s_gamma_trans_alpha, \
     s_h0); 
}

bool Manager::update_boundaries(const int group_ptr) {
   cout << "Total bounds is " << Bound::index_counter << endl;
   cout << "Total segs is " << Segment::counter << endl;
   //cout << "UB: number of clusters is " << Cluster::counter <<	
   //     ", to double check " << clusters.size() << endl;
   vector<Bound*>::iterator iter_bounds = bounds.begin();
   int i = group_ptr == 0 ? 0 : batch_groups[group_ptr - 1];
   Sampler::offset = bounds[i] -> get_start_frame_index();
   for (; i < batch_groups[group_ptr]; ++i) {
   //for(iter_bounds = bounds.begin(); iter_bounds != bounds.end(); 
   //  ++iter_bounds) {
     cout << "UB: number of clusters is " << Cluster::counter <<	\
       ", to double check " << clusters.size() << endl;

      if (!sampler.sample_boundary(iter_bounds + i, segments, clusters)) {
         Segment* parent = (*iter_bounds) -> get_parent();
         cout << "Cannot update bound " << parent -> get_tag() 
              << "frame " << parent -> get_start_frame() << " to "
              << parent -> get_end_frame() << endl;
         return false;
      }
   }
   return true;
}

void Manager::update_clusters(const bool to_precompute, const int group_ptr) {
   vector<Cluster*>::iterator iter_clusters = clusters.begin();
   for (unsigned int k = 0; k < clusters.size(); ++k) {
     cout << "Cluster " << k << endl;
      // clusters[k] -> show_member_len();
      if (clusters[k] -> get_age() >= 500000000000 && \
	  clusters[k] -> get_member_num() <= 100) {
	cout << "old cluster" << endl;

         int cluster_id = clusters[k] -> get_cluster_id();
         int member_num = clusters[k] -> get_member_num(); 
         delete clusters[k]; 
         clusters.erase(iter_clusters + k);
         --Cluster::counter;
         Segment::counter -= member_num;
         list<Segment*>::iterator iter_segments = segments.begin();
         for (; iter_segments != segments.end(); ++iter_segments) {
            if ((*iter_segments) -> get_cluster_id() == cluster_id) {
	      ++Segment::counter;
	      Cluster* new_c = sampler.sample_just_cluster(*(*iter_segments), clusters);
	      sampler.sample_more_than_cluster(*(*iter_segments), clusters, new_c); 
            }
         }
      }
      else {
	cout << "not old cluster" << endl;

         sampler.sample_hmm_parameters(*clusters[k]);
	cout << "params sampled" << endl;
         clusters[k] -> update_age();
	cout << "age updated" << endl;
         if (to_precompute) {
	   cout << "precompute" << endl;
            vector<Bound*>::iterator iter_bounds = bounds.begin();
            int i = group_ptr == 0 ? 0 : batch_groups[group_ptr - 1];
	    cout << "group_ptr" << endl;
            Bound* start_bound = *(iter_bounds + i);
            Bound* end_bound = *(iter_bounds + batch_groups[group_ptr] - 1);
            int start_index = start_bound -> get_start_frame_index();
            int end_index = end_bound -> get_start_frame_index() + \
                         end_bound -> get_frame_num();
            const float* sec_data[end_index - start_index];
	    cout << "indices" << endl;
            for (int j = start_index; j < end_index; ++j) {
               sec_data[j - start_index] = data[j];
            }
	    cout << "loop" << endl;
            sampler.precompute(clusters[k], end_index - start_index, sec_data);
	    cout << "precomputed" << endl;

            sampler.set_precompute_status(clusters[k], to_precompute);
         }
      }
   }
}

void Manager::gibbs_sampling(const int num_iter, const string result_dir) {
   for (int i = 0; i <= num_iter; ++i) {
      /*
      if (i <= 10000) {
         Sampler::annealing = 10 - (i / (num_iter / 10));
      }
      else {
         Sampler::annealing = 10.1;
      }
      */
      Sampler::annealing = 10.1;
      cout << "starting the " << i << " th iteration..." << endl;
      cout << "Total number of clusters is " << Cluster::counter << \
        ", to double check " << clusters.size() << endl;
      cout << "Updating clusters..." << endl;
      update_clusters(true, (i % batch_groups.size()));
      cout << "New number of clusters is " << Cluster::counter << \
        ", to double check " << clusters.size() << endl;
      cout << "Updating boundaries..." << endl;
      if (!update_boundaries(i % batch_groups.size())) {
         cout << "Cannot update boundaries..." << endl;
         return;
      }
      if (!(i % 100) && i != 0) {
         list<Segment*>::iterator iter_segments; 
         for (iter_segments = segments.begin(); iter_segments != segments.end(); \
               ++iter_segments) {
            stringstream num_to_string;
            num_to_string << i;
            (*iter_segments) -> write_class_label(result_dir + "/" + num_to_string.str());
         }
      }
      if (!(i % 100) && i != 0) {
         stringstream num_to_string;
         num_to_string << i;
         string fsnapshot = result_dir + "/" + \
                            num_to_string.str() + "/snapshot";
         cout << "Writing out to " << fsnapshot << " ..." << endl;
         if (!state_snapshot(fsnapshot)) {
            cout << "Cannot open " << fsnapshot << 
              ". Please make sure the path exists" << endl; 
            return;
         }
         vector<Cluster*>::iterator iter_clusters;
         for (iter_clusters = clusters.begin(); iter_clusters != clusters.end(); \
           ++iter_clusters) {
            (*iter_clusters) -> state_snapshot(fsnapshot);
         }
      }
      /*
      if (((num_iter - i <= 1000) && !(i % 100)) || i == 10 || (i % 500 == 0)) {
         stringstream num_to_string;
         num_to_string << i;
         string fsnapshot = result_dir + "/" + \
                            num_to_string.str() + "/snapshot";
         cout << "Writing out to " << fsnapshot << " ..." << endl;
         if (!state_snapshot(fsnapshot)) {
            cout << "Cannot open " << fsnapshot << 
              ". Please make sure the path exists" << endl; 
            return;
         }
         for (iter_clusters = clusters.begin(); iter_clusters != clusters.end(); \
           ++iter_clusters) {
            (*iter_clusters) -> state_snapshot(fsnapshot);
         }
      }
      */
   }
}

bool Manager::state_snapshot(const string& fn) {
   ofstream fout(fn.c_str(), ios::out);
   if (!fout.good()) {
      return false;
   }
   int data_counter = Segment::counter;
   fout.write(reinterpret_cast<char*> (&data_counter), sizeof(int));
   int cluster_counter = Cluster::counter;
   fout.write(reinterpret_cast<char*> (&cluster_counter), sizeof(int));
   /*
   int aval_id = Cluster::aval_id;
   fout.write(reinterpret_cast<char*>(&aval_id), sizeof(int));
   int cluster_counter = Cluster::counter;
   fout.write(reinterpret_cast<char*>(&cluster_counter), sizeof(int));
   int aval_data = Segment::counter;
   fout.write(reinterpret_cast<char*>(&aval_data), sizeof(int));
   int num_clusters = clusters.size();
   fout.write(reinterpret_cast<char*>(&num_clusters), sizeof(int));
   cout << num_clusters << endl;
   */
   fout.close();
   return true;
}

bool Manager::load_in_model(const string& fname, const int threshold) {
   ifstream fin(fname.c_str(), ios::binary);
   int data_num;
   int cluster_num;
   if (!fin.good()) {
      cout << fname << " cannot be opened." << endl;
      return false;
   }
   fin.read(reinterpret_cast<char*> (&data_num), sizeof(int));
   fin.read(reinterpret_cast<char*> (&cluster_num), sizeof(int));
   cout << "number of clusters " << cluster_num << endl;
   for (int i = 0; i < cluster_num; ++i) {
      int member_num;
      int state_num;
      int vector_dim;
      fin.read(reinterpret_cast<char*> (&member_num), sizeof(int));
      fin.read(reinterpret_cast<char*> (&state_num), sizeof(int));
      fin.read(reinterpret_cast<char*> (&vector_dim), sizeof(int));
      Cluster* new_cluster = new Cluster(state_num, vector_dim);
      new_cluster -> set_member_num(member_num);
      float trans[state_num * (state_num + 1)];
      fin.read(reinterpret_cast<char*> (trans), sizeof(float) * \
        state_num * (state_num + 1));
      new_cluster -> set_trans(trans);
      float weights[vector_dim];
      for (int j = 0; j < state_num; ++j) {
	fin.read(reinterpret_cast<char*> (weights),	\
		 sizeof(float) * vector_dim);
	//new_cluster -> set_weights(j,weights);
      }
      if (new_cluster -> get_member_num() > threshold) {
         clusters.push_back(new_cluster);
      }
      else {
         data_num -= new_cluster -> get_member_num();
         delete new_cluster;
      }
   }
   for (unsigned int i = 0; i < clusters.size(); ++i) {
      clusters[i] -> set_member_num(0);
   }
   Cluster::counter = clusters.size();
   fin.close();
   return true;

}


Cluster* Manager::find_cluster(const int c_id) {
   for (unsigned int i = 0; i < clusters.size(); ++i) {
      if (clusters[i] -> get_cluster_id() == c_id) {
         return clusters[i];
      }
   }
   return NULL;
}

bool Manager::load_in_data(const string& fnbound_list, const int g_size) {
   group_size = g_size;
   ifstream fbound_list(fnbound_list.c_str(), ifstream::in);
   if (!fbound_list.is_open()) {
      return false;
   }
   cout << "file opened" << endl;
   int input_counter = 0;
   while (fbound_list.good()) {
      string fn_index;
      string fn_data;
      fbound_list >> fn_index;
      fbound_list >> fn_data;
      if (fn_index != "" && fn_data != "") {
         ++input_counter;
         ifstream findex(fn_index.c_str(), ifstream::in);
         ifstream fdata(fn_data.c_str(), ifstream::binary);
         string basename = get_basename(fn_data);
         vector<Bound*> a_seg;
         if (!findex.is_open()) {
            return false;
         }
         if (!fdata.is_open()) {
            return false;
         }
         int start = 0;
         int end = 0;
         int cluster_label;
         cout << "Loading " << fn_data << "..." << endl;
         int total_frame_num;
         findex >> total_frame_num;
         while (end != total_frame_num - 1) {
            findex >> start;
            findex >> end;
            findex >> cluster_label; 
            int frame_num = end - start + 1;
            float** frame_data = new float*[frame_num];
            for (int i = 0; i < frame_num; ++i) {
               frame_data[i] = new float[s_dim];
               fdata.read(reinterpret_cast<char*>(frame_data[i]), \
                    sizeof(float) * s_dim);
            }
            bool utt_end = false;
            if (end == total_frame_num - 1) {
               utt_end = true;
            }
            Bound* new_bound = new Bound(start, end, s_dim, utt_end);
            new_bound -> set_data(frame_data);
            new_bound -> set_index(Bound::index_counter);
            new_bound -> set_start_frame_index(Bound::total_frames);
            ++Bound::index_counter;
            Bound::total_frames += end - start + 1;
            for(int i = 0; i < frame_num; ++i) {
               delete[] frame_data[i];
            }
            delete[] frame_data;
            if (frame_num) {
               bounds.push_back(new_bound);
               a_seg.push_back(new_bound);
               // Sampler::sample_boundary(Bound*) is for sampling from prior
               bool phn_end = false;
               if (cluster_label != -1 || utt_end) {
                  phn_end = true;
               }
               new_bound -> set_phn_end(phn_end);
               if (phn_end) {
                  Segment* new_segment = new Segment(basename, a_seg);
                  ++Segment::counter;
                  segments.push_back(new_segment);
                  Cluster* new_c = find_cluster(cluster_label);
                  sampler.sample_more_than_cluster(*new_segment, clusters, new_c);
                  vector<Bound*>::iterator iter_members = a_seg.begin();
                  for(; iter_members != a_seg.end(); ++iter_members) {
                     (*iter_members) -> set_parent(new_segment);
                  }
                  new_segment -> change_hash_status(false);
                  cout << Segment::counter << " segments..." << endl;
                  cout << Cluster::counter << " clusters..." << endl;
                  a_seg.clear();
               }
            }
            else {
               delete new_bound;
               Bound::index_counter--;
            }
         }
         if (a_seg.size()) {
             cout << "Not cleaned" << endl;
             Segment* new_segment = new Segment(basename, a_seg);
             ++Segment::counter;
             segments.push_back(new_segment);
             Cluster* new_c = sampler.sample_just_cluster(*new_segment, clusters);
             sampler.sample_more_than_cluster(*new_segment, clusters, new_c);
             vector<Bound*>::iterator iter_members = a_seg.begin();
             for(; iter_members != a_seg.end(); ++iter_members) {
                (*iter_members) -> set_parent(new_segment);
             }
             cout << Segment::counter << " segments..." << endl;
             cout << Cluster::counter << " clusters..." << endl;
             a_seg.clear();
         }
         vector<Bound*>::iterator to_last = bounds.end();
         to_last--;
         (*to_last) -> set_utt_end(true);
         (*to_last) -> set_phn_end(true);
         findex.close();
         fdata.close();
      }
      if (!(input_counter % group_size) && fn_index != "" && fn_data != "") {
         batch_groups.push_back(bounds.size());
         cout << input_counter << " and " << bounds.size() << endl;
      }
   }
   if (input_counter % group_size) {
      batch_groups.push_back(bounds.size());
      cout << input_counter << " and " << bounds.size() << endl;
   }
   fbound_list.close();
   load_data_to_matrix(); 
   return true;

}

bool Manager::load_snapshot(const string& fn_snapshot, const string& fn_data, \
  const int g_size) {
   if (!load_in_model(fn_snapshot, 0)) {
      cout << "Cannot load in snapshot" << endl;
      return false;
   }
   if (!load_in_data(fn_data, g_size)) {
   //if (!load_bounds(fn_data, g_size)) { // mine?
      cout << "Cannot load in data file." << endl;
      return false;
   }
   for (unsigned int c = 0; c < clusters.size(); ++c) {
      cout << "print out" << endl;
      cout << clusters[c] -> get_member_num() << endl;
      cout << clusters[c] -> get_cluster_id() << endl;
   }
   return true;
}

Manager::~Manager() {
   delete[] data; 
   vector<Bound*>::iterator iter_bounds;
   iter_bounds = bounds.begin();
   for (;iter_bounds != bounds.end(); ++iter_bounds) {
      delete *iter_bounds;
   }
   list<Segment*>::iterator iter_segments;
   iter_segments = segments.begin();
   for (;iter_segments != segments.end(); ++iter_segments) {
      delete *iter_segments;
   }
   segments.clear();
   vector<Cluster*>::iterator iter_clusters;
   iter_clusters = clusters.begin();
   for (; iter_clusters != clusters.end(); ++iter_clusters) {
      delete *iter_clusters;
   }
   clusters.clear();
}

