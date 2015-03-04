#pragma once


#include "GMMClassifier.h"


namespace SRS{


template<class ImageType>
  class PotentialInstantiations{
  
 private:
   typedef SegmentationGMMClassifier<ImageType> segClassGMMType;
   typedef MultilabelSegmentationGMMClassifier<ImageType> segClassMultilabelGMMType;
  
 };



}//namespace
