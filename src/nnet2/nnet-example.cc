// nnet2/nnet-example.cc

// Copyright 2012-2013  Johns Hopkins University (author: Daniel Povey)
//                2014  Vimal Manohar

// See ../../COPYING for clarification regarding multiple authors
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//  http://www.apache.org/licenses/LICENSE-2.0
//
// THIS CODE IS PROVIDED *AS IS* BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
// KIND, EITHER EXPRESS OR IMPLIED, INCLUDING WITHOUT LIMITATION ANY IMPLIED
// WARRANTIES OR CONDITIONS OF TITLE, FITNESS FOR A PARTICULAR PURPOSE,
// MERCHANTABLITY OR NON-INFRINGEMENT.
// See the Apache 2 License for the specific language governing permissions and
// limitations under the License.

#include "nnet2/nnet-example.h"
#include "lat/lattice-functions.h"
#include "hmm/posterior.h"

namespace kaldi {
namespace nnet2 {

// This function returns true if the example has labels which, for each frame,
// have a single element with probability one; and if so, it outputs them to the
// vector in the associated pointer.  This enables us to write the egs more
// compactly to disk in this common case.
bool HasSimpleLabels(
    const NnetExample &eg,
    std::vector<int32> *simple_labels) {
  size_t num_frames = eg.labels.size();
  for (int32 t = 0; t < num_frames; t++)
    if (eg.labels[t].size() != 1 || eg.labels[t][0].second != 1.0)
      return false;
  simple_labels->resize(num_frames);
  for (int32 t = 0; t < num_frames; t++)
    (*simple_labels)[t] = eg.labels[t][0].first;
  return true;
}

void ConvertToPdf(const std::vector<int32> &ali, 
                  const TransitionModel &tmodel,
                  std::vector<int32> *pdf_ali) {
  pdf_ali->clear();
  pdf_ali->resize(ali.size());
  for (size_t i = 0; i < ali.size(); i++) {
    (*pdf_ali)[i] = tmodel.TransitionIdToPdf(ali[i]);
  }
}

void ConvertToPhone(const std::vector<int32> &ali, 
                    const TransitionModel &tmodel,
                    std::vector<int32> *phone_ali) {
  phone_ali->clear();
  phone_ali->resize(ali.size());
  for (size_t i = 0; i < ali.size(); i++) {
    (*phone_ali)[i] = tmodel.TransitionIdToPhone(ali[i]);
  }
}

void ConvertCompactLatticeToPhonesPerFrame(const TransitionModel &trans,
                                           CompactLattice *clat) {
  typedef CompactLatticeArc Arc;
  typedef Arc::Weight Weight;
  int32 num_states = clat->NumStates();
  for (int32 state = 0; state < num_states; state++) {
    for (fst::MutableArcIterator<CompactLattice> aiter(clat, state);
         !aiter.Done();
         aiter.Next()) {
      Arc arc(aiter.Value());
      std::vector<int32> phone_seq;
      const std::vector<int32> &tid_seq = arc.weight.String();
      for (std::vector<int32>::const_iterator iter = tid_seq.begin();
           iter != tid_seq.end(); ++iter) {
        phone_seq.push_back(*iter);
      }
      arc.weight.SetString(phone_seq);
      aiter.SetValue(arc);
    } // end looping over arcs
    Weight f = clat->Final(state);
    if (f != Weight::Zero()) {
      std::vector<int32> phone_seq;
      const std::vector<int32> &tid_seq = f.String();
      for (std::vector<int32>::const_iterator iter = tid_seq.begin();
           iter != tid_seq.end(); ++iter) {
        phone_seq.push_back(trans.TransitionIdToPhone(*iter));
      }
      f.SetString(phone_seq);
      clat->SetFinal(state, f);
    }
  }  // end looping over states
}

void ConvertCompactLatticeToPdfsPerFrame(const TransitionModel &trans,
                                         CompactLattice *clat) {
  typedef CompactLatticeArc Arc;
  typedef Arc::Weight Weight;
  int32 num_states = clat->NumStates();
  for (int32 state = 0; state < num_states; state++) {
    for (fst::MutableArcIterator<CompactLattice> aiter(clat, state);
         !aiter.Done();
         aiter.Next()) {
      Arc arc(aiter.Value());
      std::vector<int32> pdf_seq;
      const std::vector<int32> &tid_seq = arc.weight.String();
      for (std::vector<int32>::const_iterator iter = tid_seq.begin();
           iter != tid_seq.end(); ++iter) {
        pdf_seq.push_back(trans.TransitionIdToPdf(*iter));
      }
      arc.weight.SetString(pdf_seq);
      aiter.SetValue(arc);
    } // end looping over arcs
    Weight f = clat->Final(state);
    if (f != Weight::Zero()) {
      std::vector<int32> pdf_seq;
      const std::vector<int32> &tid_seq = f.String();
      for (std::vector<int32>::const_iterator iter = tid_seq.begin();
           iter != tid_seq.end(); ++iter) {
        pdf_seq.push_back(trans.TransitionIdToPdf(*iter));
      }
      f.SetString(pdf_seq);
      clat->SetFinal(state, f);
    }
  }  // end looping over states
}

void NnetExample::Write(std::ostream &os, bool binary) const {
  // Note: weight, label, input_frames and spk_info are members.  This is a
  // struct.
  WriteToken(os, binary, "<NnetExample>");

  // At this point, we write <Lab1> if we have "simple" labels, or
  // <Lab2> in general.  Previous code (when we had only one frame of
  // labels) just wrote <Labels>.
  std::vector<int32> simple_labels;
  if (HasSimpleLabels(*this, &simple_labels)) {
    WriteToken(os, binary, "<Lab1>");
    WriteIntegerVector(os, binary, simple_labels);
  } else {
    WriteToken(os, binary, "<Lab2>");
    int32 num_frames = labels.size();
    WriteBasicType(os, binary, num_frames);
    for (int32 t = 0; t < num_frames; t++) {
      int32 size = labels[t].size();
      WriteBasicType(os, binary, size);
      for (int32 i = 0; i < size; i++) {
        WriteBasicType(os, binary, labels[t][i].first);
        WriteBasicType(os, binary, labels[t][i].second);
      }
    }
  }
  WriteToken(os, binary, "<InputFrames>");
  input_frames.Write(os, binary);
  WriteToken(os, binary, "<LeftContext>");
  WriteBasicType(os, binary, left_context);
  WriteToken(os, binary, "<SpkInfo>");
  spk_info.Write(os, binary);
  WriteToken(os, binary, "</NnetExample>");
}

void NnetExample::Read(std::istream &is, bool binary) {
  // Note: weight, label, input_frames, left_context and spk_info are members.
  // This is a struct.
  ExpectToken(is, binary, "<NnetExample>");

  std::string token;
  ReadToken(is, binary, &token);
  if (!strcmp(token.c_str(), "<Lab1>")) {  // simple label format
    std::vector<int32> simple_labels;
    ReadIntegerVector(is, binary, &simple_labels);
    labels.resize(simple_labels.size());
    for (size_t i = 0; i < simple_labels.size(); i++) {
      labels[i].resize(1);
      labels[i][0].first = simple_labels[i];
      labels[i][0].second = 1.0;
    }
  } else if (!strcmp(token.c_str(), "<Lab2>")) {  // generic label format
    int32 num_frames;
    ReadBasicType(is, binary, &num_frames);
    KALDI_ASSERT(num_frames > 0);
    labels.resize(num_frames);
    for (int32 t = 0; t < num_frames; t++) {
      int32 size;
      ReadBasicType(is, binary, &size);
      KALDI_ASSERT(size >= 0);
      labels[t].resize(size);
      for (int32 i = 0; i < size; i++) {
        ReadBasicType(is, binary, &(labels[t][i].first));
        ReadBasicType(is, binary, &(labels[t][i].second));
      }
    }
  } else if (!strcmp(token.c_str(), "<Labels>")) {  // back-compatibility
    labels.resize(1);  // old format had 1 frame of labels.
    int32 size;
    ReadBasicType(is, binary, &size);
    labels[0].resize(size);
    for (int32 i = 0; i < size; i++) {
      ReadBasicType(is, binary, &(labels[0][i].first));
      ReadBasicType(is, binary, &(labels[0][i].second));
    }
  } else {
    KALDI_ERR << "Expected token <Lab1>, <Lab2> or <Labels>, got " << token;
  }
  ExpectToken(is, binary, "<InputFrames>");
  input_frames.Read(is, binary);
  ExpectToken(is, binary, "<LeftContext>"); // Note: this member is
  // recently added, but I don't think we'll get too much back-compatibility
  // problems from not handling the old format.
  ReadBasicType(is, binary, &left_context);
  ExpectToken(is, binary, "<SpkInfo>");
  spk_info.Read(is, binary);
  ExpectToken(is, binary, "</NnetExample>");
}

void NnetExample::SetLabelSingle(int32 frame, int32 pdf_id, BaseFloat weight) {
  KALDI_ASSERT(static_cast<size_t>(frame) < labels.size());
  labels[frame].clear();
  labels[frame].push_back(std::make_pair(pdf_id, weight));
}

int32 NnetExample::GetLabelSingle(int32 frame, BaseFloat *weight) const {
  BaseFloat max = -1.0;
  int32 pdf_id = -1;
  KALDI_ASSERT(static_cast<size_t>(frame) < labels.size());
  for (int32 i = 0; i < labels[frame].size(); i++) {
    if (labels[frame][i].second > max) {
      pdf_id = labels[frame][i].first;
      max = labels[frame][i].second;
    }
  }
  if (weight != NULL) *weight = max;
  return pdf_id;
}



static bool nnet_example_warned_left = false, nnet_example_warned_right = false;

// Self-constructor that can reduce the number of frames and/or context.
NnetExample::NnetExample(const NnetExample &input,
                         int32 start_frame,
                         int32 new_num_frames,
                         int32 new_left_context,
                         int32 new_right_context): spk_info(input.spk_info) {
  int32 num_label_frames = input.labels.size();
  if (start_frame < 0) start_frame = 0;  // start_frame is offset in the labeled
                                         // frames.
  KALDI_ASSERT(start_frame < num_label_frames);
  if (start_frame + new_num_frames > num_label_frames || new_num_frames == -1)
    new_num_frames = num_label_frames - start_frame;
  // compute right-context of input.
  int32 input_right_context =
      input.input_frames.NumRows() - input.left_context - num_label_frames;
  if (new_left_context == -1) new_left_context = input.left_context;
  if (new_right_context == -1) new_right_context = input_right_context;
  if (new_left_context > input.left_context) {
    if (!nnet_example_warned_left) {
      nnet_example_warned_left = true;
      KALDI_WARN << "Requested left-context " << new_left_context
                 << " exceeds input left-context " << input.left_context
                 << ", will not warn again.";
    }
    new_left_context = input.left_context;
  }
  if (new_right_context > input_right_context) {
    if (!nnet_example_warned_right) {
      nnet_example_warned_right = true;
      KALDI_WARN << "Requested right-context " << new_right_context
                 << " exceeds input right-context " << input_right_context
                 << ", will not warn again.";
    }
    new_right_context = input_right_context;
  }

  int32 new_tot_frames = new_left_context + new_num_frames + new_right_context,
      left_frames_lost = (input.left_context - new_left_context) + start_frame;
  
  CompressedMatrix new_input_frames(input.input_frames,
                                    left_frames_lost,
                                    new_tot_frames,
                                    0, input.input_frames.NumCols());
  new_input_frames.Swap(&input_frames);  // swap with class-member.
  left_context = new_left_context;  // set class-member.
  labels.clear();
  labels.insert(labels.end(),
                input.labels.begin() + start_frame,
                input.labels.begin() + start_frame + new_num_frames);
}

void ExamplesRepository::AcceptExamples(
    std::vector<NnetExample> *examples) {
  KALDI_ASSERT(!examples->empty());
  empty_semaphore_.Wait();
  KALDI_ASSERT(examples_.empty());
  examples_.swap(*examples);
  full_semaphore_.Signal();
}

void ExamplesRepository::ExamplesDone() {
  empty_semaphore_.Wait();
  KALDI_ASSERT(examples_.empty());
  done_ = true;
  full_semaphore_.Signal();
}

bool ExamplesRepository::ProvideExamples(
    std::vector<NnetExample> *examples) {
  full_semaphore_.Wait();
  if (done_) {
    KALDI_ASSERT(examples_.empty());
    full_semaphore_.Signal(); // Increment the semaphore so
    // the call by the next thread will not block.
    return false; // no examples to return-- all finished.
  } else {
    KALDI_ASSERT(!examples_.empty() && examples->empty());
    examples->swap(examples_);
    empty_semaphore_.Signal();
    return true;
  }
}

void DiscriminativeNnetExample::Write(std::ostream &os,
                                bool binary) const {
  // Note: weight, num_ali, den_lat, input_frames, left_context and spk_info are
  // members.  This is a struct.
  WriteToken(os, binary, "<DiscriminativeNnetExample>");
  WriteToken(os, binary, "<Weight>");
  WriteBasicType(os, binary, weight);
  WriteToken(os, binary, "<NumFrames>");
  WriteBasicType(os, binary, num_frames);

  WriteToken(os, binary, "<NumAli>");
  WriteIntegerVector(os, binary, num_ali);
 
  if (num_lat_present) {
    WriteToken(os, binary, "<NumLat>");
  
    if (!WriteCompactLattice(os, binary, num_lat)) {
      // We can't return error status from this function so we
      // throw an exception. 
      KALDI_ERR << "Error writing numerator lattice to stream";
    }
  } 

  WriteToken(os, binary, "<NumPost>");
  WritePosterior(os, binary, num_post);

  WriteToken(os, binary, "<OracleAli>");
  WriteIntegerVector(os, binary, oracle_ali);

  WriteToken(os, binary, "<FrameWeights>");
  Vector<BaseFloat> frame_weights(weights.size());
  for (size_t i = 0; i < weights.size(); i++) {
    frame_weights(i) = weights[i];
  }
  frame_weights.Write(os, binary);

  WriteToken(os, binary, "<DenLat>");
  if (!WriteCompactLattice(os, binary, den_lat)) {
    // We can't return error status from this function so we
    // throw an exception. 
    KALDI_ERR << "Error writing CompactLattice to stream";
  }
  WriteToken(os, binary, "<InputFrames>");
  {
    CompressedMatrix cm(input_frames); // Note: this can be read as a regular
                                       // matrix.
    cm.Write(os, binary);
  }
  WriteToken(os, binary, "<LeftContext>");
  WriteBasicType(os, binary, left_context);
  WriteToken(os, binary, "<SpkInfo>");
  spk_info.Write(os, binary);
  WriteToken(os, binary, "</DiscriminativeNnetExample>");
}

void DiscriminativeNnetExamplePhoneOrPdf::Write(std::ostream &os,
                                  bool binary) const {
  KALDI_ASSERT(phone_or_pdf == "pdf" || phone_or_pdf == "phone");
  bool convert_to_pdf = (phone_or_pdf == "pdf");

  // Note: weight, num_ali, den_lat, input_frames, left_context and spk_info are
  // members.  This is a struct.
  WriteToken(os, binary, "<DiscriminativeNnetExample>");
  WriteToken(os, binary, "<Weight>");
  WriteBasicType(os, binary, weight);
  WriteToken(os, binary, "<NumFrames>");
  WriteBasicType(os, binary, num_frames);

  WriteToken(os, binary, "<NumAli>");
  std::vector<int32> alignment;
  if (convert_to_pdf)
    ConvertToPdf(num_ali, tmodel, &alignment);
  else
    ConvertToPhone(num_ali, tmodel, &alignment);
  WriteIntegerVector(os, binary, alignment);
 
  if (num_lat_present) {
    WriteToken(os, binary, "<NumLat>");
  
    CompactLattice clat = num_lat;
    if (convert_to_pdf)
      ConvertCompactLatticeToPdfsPerFrame(tmodel, &clat);
    else 
      ConvertCompactLatticeToPhonesPerFrame(tmodel, &clat);

    if (!WriteCompactLattice(os, binary, clat)) {
      // We can't return error status from this function so we
      // throw an exception. 
      KALDI_ERR << "Error writing numerator lattice to stream";
    }
  } 

  WriteToken(os, binary, "<NumPost>");
  WritePosterior(os, binary, num_post);

  WriteToken(os, binary, "<OracleAli>");
  WriteIntegerVector(os, binary, oracle_ali);
  if (convert_to_pdf)
    ConvertToPdf(oracle_ali, tmodel, &alignment);
  else
    ConvertToPhone(oracle_ali, tmodel, &alignment);
  WriteIntegerVector(os, binary, alignment);

  WriteToken(os, binary, "<FrameWeights>");
  Vector<BaseFloat> frame_weights(weights.size());
  for (size_t i = 0; i < weights.size(); i++) {
    frame_weights(i) = weights[i];
  }
  frame_weights.Write(os, binary);

  WriteToken(os, binary, "<DenLat>");
  CompactLattice clat = den_lat;
  if (convert_to_pdf)
    ConvertCompactLatticeToPdfsPerFrame(tmodel, &clat);
  else 
    ConvertCompactLatticeToPhonesPerFrame(tmodel, &clat);
  if (!WriteCompactLattice(os, binary, clat)) {
    // We can't return error status from this function so we
    // throw an exception. 
    KALDI_ERR << "Error writing CompactLattice to stream";
  }
  WriteToken(os, binary, "<InputFrames>");
  {
    CompressedMatrix cm(input_frames); // Note: this can be read as a regular
                                       // matrix.
    cm.Write(os, binary);
  }
  WriteToken(os, binary, "<LeftContext>");
  WriteBasicType(os, binary, left_context);
  WriteToken(os, binary, "<SpkInfo>");
  spk_info.Write(os, binary);
  WriteToken(os, binary, "</DiscriminativeNnetExample>");
}

void DiscriminativeNnetExample::Read(std::istream &is,
                                             bool binary) {
  // Note: weight, num_ali, den_lat, input_frames, left_context and spk_info are
  // members.  This is a struct.
  std::string token;
  ReadToken(is, binary, &token);
  
  if (token == "<DiscriminativeUnsupervisedNnetExample>") {
    // Old format for unsupervised examples
    ExpectToken(is, binary, "<Weight>");
    ReadBasicType(is, binary, &weight);
    ExpectToken(is, binary, "<NumFrames>");
    ReadBasicType(is, binary, &num_frames);

    CompactLattice *lat_tmp = NULL;
    if (!ReadCompactLattice(is, binary, &lat_tmp) || lat_tmp == NULL) {
      // We can't return error status from this function so we
      // throw an exception. 
      KALDI_ERR << "Error reading CompactLattice from stream";
    }
    den_lat = *lat_tmp;
    delete lat_tmp;
    std::string token;
    ReadToken(is, binary, &token);
    while (token != "<InputFrames>") {
      if (token == "<Ali>") {
        ReadIntegerVector(is, binary, &num_ali);
        ReadToken(is, binary, &token);
      } else if (token == "<Oracle>") {
        ReadIntegerVector(is, binary, &oracle_ali);
        ReadToken(is, binary, &token);
      } else if (token == "<Weights>") {
        Vector<BaseFloat> temp;
        temp.Read(is, binary);
        weights.resize(temp.Dim());
        for (size_t i = 0; i < temp.Dim(); i++) {
          weights[i] = temp(i);
        }
        ReadToken(is, binary, &token);
      } else {
        KALDI_ERR << "Unexpected token " << token 
          << "; Expecting <Ali> or or <Oracle> or <Weights> or <InputFrames>";
      }
    }
    input_frames.Read(is, binary);
    ExpectToken(is, binary, "<LeftContext>");
    ReadBasicType(is, binary, &left_context);
    ExpectToken(is, binary, "<SpkInfo>");
    spk_info.Read(is, binary);
    ExpectToken(is, binary, "</DiscriminativeUnsupervisedNnetExample>");
    return;
  }

  if (token != "<DiscriminativeNnetExample>") {
    KALDI_ERR << "Expected token to be <DiscriminativeNnetExample> or "
      << "<DiscriminativeUnsupervisedNnetExample>; got " << token;
  }

  ExpectToken(is, binary, "<Weight>");
  ReadBasicType(is, binary, &weight);
  ReadToken(is, binary, &token);
  if (token == "<NumAli>") {
    ReadIntegerVector(is, binary, &num_ali);
    CompactLattice *den_lat_tmp = NULL;
    if (!ReadCompactLattice(is, binary, &den_lat_tmp) || den_lat_tmp == NULL) {
      // We can't return error status from this function so we
      // throw an exception. 
      KALDI_ERR << "Error reading CompactLattice from stream";
    }
    den_lat = *den_lat_tmp;
    delete den_lat_tmp;
    ExpectToken(is, binary, "<InputFrames>");
    input_frames.Read(is, binary);
    ExpectToken(is, binary, "<LeftContext>");
    ReadBasicType(is, binary, &left_context);
    ExpectToken(is, binary, "<SpkInfo>");
    spk_info.Read(is, binary);
    ExpectToken(is, binary, "</DiscriminativeNnetExample>");
    num_frames = num_ali.size();
    num_lat_present = false;
  } else {
    if (token != "<NumFrames>")
      KALDI_ERR << "Expected token <NumFrames> or <NumAli>; got " << token;
    ReadBasicType(is, binary, &num_frames);
    ExpectToken(is, binary, "<NumAli>");
    ReadIntegerVector(is, binary, &num_ali);
    ReadToken(is, binary, &token);
    while (token != "<DenLat>") {
      if (token == "<NumLat>") {
        CompactLattice *num_lat_tmp = NULL;
        if (!ReadCompactLattice(is, binary, &num_lat_tmp) || num_lat_tmp == NULL) {
          // We can't return error status from this function so we
          // throw an exception. 
          KALDI_ERR << "Error reading CompactLattice from stream";
        }
        num_lat = *num_lat_tmp;
        delete num_lat_tmp;
        num_lat_present = true;
      } else if (token == "<NumPost>") {
        ReadPosterior(is, binary, &num_post);
      } else if (token == "<OracleAli>") {
        ReadIntegerVector(is, binary, &oracle_ali);
      } else if (token == "<FrameWeights>") {
        Vector<BaseFloat> frame_weights;
        frame_weights.Read(is, binary);
        weights.clear();
        weights.insert(weights.end(), frame_weights.Data(), frame_weights.Data() + frame_weights.Dim());
      } else {
        KALDI_ERR << "Expecting token to be one of "
                  << "{<NumLat>, <NumPost>, <OracleAli>, <FrameWeights>, "
                  << "<DenLat>}; got " << token;
      }
      ReadToken(is, binary, &token);
    }
    CompactLattice *den_lat_tmp = NULL;
    if (!ReadCompactLattice(is, binary, &den_lat_tmp) || den_lat_tmp == NULL) {
      // We can't return error status from this function so we
      // throw an exception. 
      KALDI_ERR << "Error reading CompactLattice from stream";
    }
    den_lat = *den_lat_tmp;
    delete den_lat_tmp;

    ExpectToken(is, binary, "<InputFrames>");
    input_frames.Read(is, binary);
    ExpectToken(is, binary, "<LeftContext>");
    ReadBasicType(is, binary, &left_context);
    ExpectToken(is, binary, "<SpkInfo>");
    spk_info.Read(is, binary);
    ExpectToken(is, binary, "</DiscriminativeNnetExample>");
  }
  Check();
}

void DiscriminativeNnetExample::Check() const {
  KALDI_ASSERT(weight > 0.0);
  KALDI_ASSERT(!num_ali.empty());
  KALDI_ASSERT(num_frames == static_cast<int32>(num_ali.size()));

  KALDI_ASSERT(num_post.size() == 0 || num_post.size() == num_frames);
  KALDI_ASSERT(oracle_ali.size() == 0 || oracle_ali.size() == num_frames);
  KALDI_ASSERT(weights.size() == 0 || weights.size() == num_frames);

  if (num_lat_present) {
    std::vector<int32> times;
    int32 num_frames_num = CompactLatticeStateTimes(den_lat, &times);
    KALDI_ASSERT(num_frames == num_frames_num);
  }

  std::vector<int32> times;
  int32 num_frames_den = CompactLatticeStateTimes(den_lat, &times);
  KALDI_ASSERT(num_frames == num_frames_den);
  KALDI_ASSERT(input_frames.NumRows() >= left_context + num_frames);
}

} // namespace nnet2
} // namespace kaldi
