// featbin/wav-reverberate.cc

// Copyright 2015  Tom Ko
//           2015  Vimal Manohar

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

#include "base/kaldi-common.h"
#include "util/common-utils.h"
#include "feat/wave-reader.h"
#include "feat/signal.h"

namespace kaldi {

/*
   This function is to repeatedly concatenate signal1 by itself 
   to match the length of signal2 and add the two signals together.
*/
void AddVectorsOfUnequalLength(const Vector<BaseFloat> &signal1, 
                               Vector<BaseFloat> *signal2,
                               Vector<BaseFloat> *signal1_added) {
  if (signal1_added)
    KALDI_ASSERT(signal2->Dim() == signal1_added->Dim());
  for (int32 po = 0; po < signal2->Dim(); po += signal1.Dim()) {
    int32 block_length = signal1.Dim();
    if (signal2->Dim() - po < block_length) block_length = signal2->Dim() - po;
    signal2->Range(po, block_length).AddVec(1.0, signal1.Range(0, block_length));
    if (signal1_added)
      signal1_added->Range(po, block_length).CopyFromVec(
                                              signal1.Range(0, block_length));
  }
}

BaseFloat MaxAbsolute(const Vector<BaseFloat> &vector) {
  return std::max(std::abs(vector.Max()), std::abs(vector.Min()));
}

inline BaseFloat ComputeEnergy(const Vector<BaseFloat> &vec) {
  return VecVec(vec, vec) / vec.Dim();
}

/* 
   Early reverberation component of the signal is composed of reflections 
   within 0.05 seconds of the direct path signal (assumed to be the peak of 
   the room impulse response). This function returns the energy in 
   this early reverberation component of the signal. 
   The input parameters to this function are the room impulse response, the signal
   and their sampling frequency respectively.
*/
BaseFloat ComputeEarlyReverbEnergy(const Vector<BaseFloat> &rir, const Vector<BaseFloat> &signal,
                                   BaseFloat samp_freq) {
  int32 peak_index = 0;
  rir.Max(&peak_index);
  KALDI_VLOG(1) << "peak index is " << peak_index;

  const float sec_before_peak = 0.001;
  const float sec_after_peak = 0.05;
  int32 early_rir_start_index = peak_index - sec_before_peak * samp_freq;
  int32 early_rir_end_index = peak_index + sec_after_peak * samp_freq;
  if (early_rir_start_index < 0) early_rir_start_index = 0;
  if (early_rir_end_index > rir.Dim()) early_rir_end_index = rir.Dim();

  int32 duration = early_rir_end_index - early_rir_start_index;
  Vector<BaseFloat> early_rir(rir.Range(early_rir_start_index, duration));
  Vector<BaseFloat> early_reverb(signal);
  FFTbasedBlockConvolveSignals(early_rir, &early_reverb);

  // compute the energy
  return ComputeEnergy(early_reverb);
}

/*
   This is the core function to do reverberation and noise addition
   on the given signal. The noise will be scaled before the addition
   to match the given signal-to-noise ratio (SNR) and it will also concatenate
   itself repeatedly to match the length of the signal.
   The input parameters to this function are the room impulse response,
   the sampling frequency, the SNR(dB), the noise and the signal respectively.
*/
void DoReverberation(const Vector<BaseFloat> &rir, BaseFloat samp_freq,
                     BaseFloat snr_db, Vector<BaseFloat> *noise,
                     Vector<BaseFloat> *signal, 
                     Vector<BaseFloat> *out_clean,
                     Vector<BaseFloat> *out_noise = NULL) {
  if (noise) {
    float input_power;
    
    if (rir.Dim() > 0) {
      input_power = ComputeEarlyReverbEnergy(rir, *signal, samp_freq);
    } else {
      input_power = ComputeEnergy(*signal);
    }
    float noise_power = VecVec(*noise, *noise) / noise->Dim();
    float scale_factor = sqrt(pow(10, -snr_db / 10.0) * input_power / noise_power);
    noise->Scale(scale_factor);
    KALDI_VLOG(1) << "Noise signal is being scaled with " << scale_factor
                  << " to generate output with SNR " << snr_db << "db\n";
  }

  if (rir.Dim() > 0)
    FFTbasedBlockConvolveSignals(rir, signal);

  if (out_clean)
    out_clean->CopyFromVec(*signal);

  if (noise) {
    AddVectorsOfUnequalLength(*noise, signal, out_noise);
  }
}

}

int main(int argc, char *argv[]) {
  try {
    using namespace kaldi;

    const char *usage =
        "Corrupts the wave files supplied via input pipe with the specified\n"
        "room-impulse response (rir_matrix) and additive noise distortions\n"
        "(specified by corresponding files).\n"
        "Usage: wav-reverberate <input-wave-file> [<rir-file>] <output-wave-file>\n"
        " e.g.: wav-reverberate clean.wav large_room_rir.wav corrupted.wav\n";

    ParseOptions po(usage);
    
    std::string noise_file;
    std::string out_clean_file;
    std::string out_noise_file;

    BaseFloat snr_db = 20;
    bool multi_channel_output = false;
    int32 input_channel = 0;
    int32 rir_channel = 0;
    int32 noise_channel = 0;
    bool normalize_output = true;
    BaseFloat volume = 0;
    BaseFloat signal_db = 0, rms_energy_amplitude = 0.8;
    bool normalize_by_amplitude = false, normalize_by_power = false;

    po.Register("multi-channel-output", &multi_channel_output,
                "Specifies if the output should be multi-channel or not");
    po.Register("input-wave-channel", &input_channel,
                "Specifies the channel to be used from input as only a "
                "single channel will be used to generate reverberated output");
    po.Register("rir-channel", &rir_channel,
                "Specifies the channel of the room impulse response, "
                "it will only be used when multi-channel-output is false");
    po.Register("noise-channel", &noise_channel,
                "Specifies the channel of the noise file, "
                "it will only be used when multi-channel-output is false");
    po.Register("noise-file", &noise_file,
                "File with additive noise");
    po.Register("snr-db", &snr_db,
                "Desired SNR(dB) of the output");
    po.Register("normalize-output", &normalize_output,
                "If true, then after reverberating and "
                "possibly adding noise, scale so that the signal "
                "energy is the same as the original input signal.");
    po.Register("volume", &volume,
                "If nonzero, a scaling factor on the signal that is applied "
                "after reverberating and possibly adding noise. "
                "If you set this option to a nonzero value, it will be as"
                "if you had also specified --normalize-output=false. "
                "If you set this option to a negative value, it will be "
                "ignored and instead the --signal-db option would be used.");
    po.Register("signal-db", &signal_db,
                "Desired signal energy after corruption. This will be used "
                "only if volume is less than 0");
    po.Register("normalize-by-amplitude", &normalize_by_amplitude, 
                "Make the maximum amplitude in the output signal to be 95% of "
                "the amplitude range possible in wave output");
    po.Register("normalize-by-power", &normalize_by_power,
                "Make the amplitude such that the RMS energy of the signal "
                "is rms-energy-amplitude fraction of the total range of "
                "amplitudes possible in wave output");
    po.Register("rms-energy-amplitude", &rms_energy_amplitude,
                "Fraction of the range of amplitides possible in wave output");
    po.Register("output-noise-file", &out_noise_file,
                "Wave file to write the output noise file just before "
                "adding it to the reverberated signal");
    po.Register("output-clean-file", &out_clean_file,
                "Wave file to write the output clean file just before "
                "adding additive noise. It may have reverberation");

    po.Read(argc, argv);
    if (po.NumArgs() != 3 && po.NumArgs() != 2) {
      po.PrintUsage();
      exit(1);
    }
    if (multi_channel_output) {
      if (rir_channel != 0 || noise_channel != 0)
        KALDI_WARN << "options for --rir-channel and --noise-channel"
                      "are ignored as --multi-channel-output is true.";
    }


    std::string input_wave_file = po.GetArg(1);
    std::string output_wave_file = po.GetArg(po.NumArgs());

    std::string rir_file;
    if (po.NumArgs() == 3)
      rir_file = po.GetArg(2);
    
    KALDI_VLOG(1) << "input-wav-file: " << input_wave_file;
    KALDI_VLOG(1) << "output-wav-file: " << output_wave_file;
    KALDI_VLOG(1) << "rir-file: " << (!rir_file.empty() ? rir_file : "None");

    WaveData input_wave;
    {
      Input ki(input_wave_file);
      input_wave.Read(ki.Stream());
    }

    const Matrix<BaseFloat> &input_matrix = input_wave.Data();
    BaseFloat samp_freq_input = input_wave.SampFreq();
    int32 num_samp_input = input_matrix.NumCols(),  // #samples in the input
          num_input_channel = input_matrix.NumRows();  // #channels in the input
    KALDI_VLOG(1) << "sampling frequency of input: " << samp_freq_input
                  << " #samples: " << num_samp_input
                  << " #channel: " << num_input_channel;
    KALDI_ASSERT(input_channel < num_input_channel);

    BaseFloat samp_freq_rir = samp_freq_input;
    int32 num_samp_rir = 0,
          num_rir_channel = 1;

    const Matrix<BaseFloat> *rir_matrix = NULL;
    WaveData rir_wave;

    if (!rir_file.empty()) {
      {
        Input ki(rir_file);
        rir_wave.Read(ki.Stream());
      }
      rir_matrix = &rir_wave.Data();

      samp_freq_rir = rir_wave.SampFreq();
      num_samp_rir = rir_matrix->NumCols();
      num_rir_channel = rir_matrix->NumRows();
      KALDI_VLOG(1) << "sampling frequency of rir: " << samp_freq_rir
                    << " #samples: " << num_samp_rir
                    << " #channel: " << num_rir_channel;
      if (!multi_channel_output) {
        KALDI_ASSERT(rir_channel < num_rir_channel);
      }
    } else {
      rir_channel = 0;
      // Cannot create multichannel output without an rir-file
      KALDI_ASSERT(!multi_channel_output);    
    }

    Matrix<BaseFloat> noise_matrix;
    if (!noise_file.empty()) {
      WaveData noise_wave;
      {
        Input ki(noise_file);
        noise_wave.Read(ki.Stream());
      }
      noise_matrix = noise_wave.Data();
      BaseFloat samp_freq_noise = noise_wave.SampFreq();
      int32 num_samp_noise = noise_matrix.NumCols(),
            num_noise_channel = noise_matrix.NumRows();
      KALDI_VLOG(1) << "sampling frequency of noise: " << samp_freq_noise
                    << " #samples: " << num_samp_noise
                    << " #channel: " << num_noise_channel;
      if (multi_channel_output) {
        KALDI_ASSERT(num_rir_channel == num_noise_channel);
      } else {
        KALDI_ASSERT(noise_channel < num_noise_channel);
      }
    }

    int32 num_output_channels = (multi_channel_output ? num_rir_channel : 1);
    Matrix<BaseFloat> out_matrix(num_output_channels, num_samp_input);

    Matrix<BaseFloat> out_clean_matrix;
    Matrix<BaseFloat> out_noise_matrix;

    for (int32 output_channel = 0; output_channel < num_output_channels; output_channel++) {
      Vector<BaseFloat> input(num_samp_input);
      input.CopyRowFromMat(input_matrix, input_channel);
      float power_before_reverb = VecVec(input, input) / input.Dim();

      int32 this_rir_channel = (multi_channel_output ? output_channel : rir_channel);
      Vector<BaseFloat> rir(num_samp_rir);

      if (rir_matrix != NULL) {
        rir.CopyRowFromMat(*rir_matrix, this_rir_channel);
        rir.Scale(1.0 / (1 << 15));
      }

      Vector<BaseFloat> noise;
      if (!noise_file.empty()) {
        noise.Resize(noise_matrix.NumCols());
        int32 this_noise_channel = (multi_channel_output ? output_channel : noise_channel);
        noise.CopyRowFromMat(noise_matrix, this_noise_channel);
      }

      Vector<BaseFloat> clean_signal(input.Dim());
      Vector<BaseFloat> noise_signal(input.Dim());
      DoReverberation(rir, samp_freq_rir, snr_db, 
          (!noise_file.empty() ? &noise : NULL), &input,
          (!out_clean_file.empty() ? &clean_signal : NULL),
          (!out_noise_file.empty() ? &noise_signal : NULL));

      float power_after_reverb = ComputeEnergy(input);

      if (volume > 0) {
        input.Scale(volume);
        if (!out_clean_file.empty())
          clean_signal.Scale(volume);
        if (!noise_file.empty()) 
          noise.Scale(volume);
      } else if (volume < 0) {
        BaseFloat scale;

        if (normalize_by_amplitude) {
          BaseFloat max = MaxAbsolute(input);

          scale = Exp( signal_db / 20.0 * Log(10.0) 
                       - Log(max) + 15.0 * Log(2.0) + Log(0.95) );
          
        } else if (normalize_by_power) {
          scale = Exp( signal_db / 20.0 * Log(10.0) 
                      - 0.5 * Log(power_after_reverb)
                      + 15.0 * Log(2.0) + Log(rms_energy_amplitude) ); 
        }
        
        input.Scale(scale);

        if (!out_clean_file.empty())
          clean_signal.Scale(scale);
        if (!noise_file.empty()) 
          noise_signal.Scale(scale);
      } else if (normalize_output)
        input.Scale(sqrt(power_before_reverb / power_after_reverb));

      out_matrix.CopyRowFromVec(input, output_channel);
      
      if (!out_clean_file.empty()) {
        if (output_channel == 0)
          out_clean_matrix.Resize(out_matrix.NumRows(), out_matrix.NumCols());
        out_clean_matrix.CopyRowFromVec(clean_signal, output_channel);
      }

      if (!out_noise_file.empty()) {
        if (output_channel == 0)
          out_noise_matrix.Resize(out_matrix.NumRows(), out_matrix.NumCols());
        out_noise_matrix.CopyRowFromVec(noise_signal, output_channel);
      }
    }

    WaveData out_wave(samp_freq_input, out_matrix);
    Output ko(output_wave_file, false);
    out_wave.Write(ko.Stream());

    if (!out_clean_file.empty()) {
      WaveData out_clean_wave(samp_freq_input, out_clean_matrix);
      Output ko(out_clean_file, false);
      out_clean_wave.Write(ko.Stream());
    }

    if (!out_noise_file.empty()) {
      WaveData out_noise_wave(samp_freq_input, out_noise_matrix);
      Output ko(out_noise_file, false);
      out_noise_wave.Write(ko.Stream());
    }

    return 0;
  } catch(const std::exception &e) {
    std::cerr << e.what();
    return -1;
  }
}

