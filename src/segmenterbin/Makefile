
all:

EXTRA_CXXFLAGS = -Wno-sign-compare
include ../kaldi.mk

BINFILES = segmentation-copy segmentation-init-from-segments \
					 segmentation-init-from-ali segmentation-select-top \
					 gmm-acc-pdf-stats-segmentation select-feats-from-segmentation \
					 gmm-est-segmentation segmentation-to-rttm segmentation-post-process \
					 gmm-update-segmentation segmentation-to-ali \
					 segmentation-create-subsegments segmentation-remove-segments \
					 segmentation-init-from-diarization segmentation-to-segments \
					 segmentation-compute-class-ctm-conf segmentation-filter-ctm

OBJFILES =



TESTFILES = 

ADDLIBS = ../hmm/kaldi-hmm.a ../gmm/kaldi-gmm.a  ../segmenter/kaldi-segmenter.a ../tree/kaldi-tree.a \
					../util/kaldi-util.a ../matrix/kaldi-matrix.a ../base/kaldi-base.a ../thread/kaldi-thread.a

include ../makefiles/default_rules.mk

