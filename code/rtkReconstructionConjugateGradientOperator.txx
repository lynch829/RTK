/*=========================================================================
 *
 *  Copyright RTK Consortium
 *
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *         http://www.apache.org/licenses/LICENSE-2.0.txt
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 *
 *=========================================================================*/

#ifndef __rtkReconstructionConjugateGradientOperator_txx
#define __rtkReconstructionConjugateGradientOperator_txx

#include "rtkReconstructionConjugateGradientOperator.h"

namespace rtk
{

template< typename TOutputImage>
ReconstructionConjugateGradientOperator<TOutputImage>
::ReconstructionConjugateGradientOperator():
  m_Geometry(NULL),
  m_IsWeighted(false),
  m_Preconditioned(false)
{
  this->SetNumberOfRequiredInputs(2);

  // Create filters
#ifdef RTK_USE_CUDA
  m_ConstantProjectionsSource = rtk::CudaConstantVolumeSource::New();
  m_ConstantVolumeSource = rtk::CudaConstantVolumeSource::New();
  m_DisplacedDetectorFilter = rtk::CudaDisplacedDetectorImageFilter::New();
#else
  m_ConstantProjectionsSource = ConstantSourceType::New();
  m_ConstantVolumeSource = ConstantSourceType::New();
  m_DisplacedDetectorFilter = DisplacedDetectorFilterType::New();
#endif
  m_MultiplyProjectionsFilter = MultiplyFilterType::New();
  m_MultiplyOutputVolumeFilter = MultiplyFilterType::New();
  m_MultiplyInputVolumeFilter = MultiplyFilterType::New();

  // Set permanent parameters
  m_ConstantProjectionsSource->SetConstant(itk::NumericTraits<typename TOutputImage::PixelType>::ZeroValue());
  m_ConstantVolumeSource->SetConstant(itk::NumericTraits<typename TOutputImage::PixelType>::ZeroValue());
  m_DisplacedDetectorFilter->SetPadOnTruncatedSide(false);

  // Set memory management options
  m_ConstantProjectionsSource->ReleaseDataFlagOn();
  m_ConstantVolumeSource->ReleaseDataFlagOn();
}

template< typename TOutputImage >
void
ReconstructionConjugateGradientOperator<TOutputImage>
::SetBackProjectionFilter (const typename BackProjectionFilterType::Pointer _arg)
{
  m_BackProjectionFilter = _arg;
}

template< typename TOutputImage >
void
ReconstructionConjugateGradientOperator<TOutputImage>
::SetForwardProjectionFilter (const typename ForwardProjectionFilterType::Pointer _arg)
{
  m_ForwardProjectionFilter = _arg;
}

template< typename TOutputImage >
void
ReconstructionConjugateGradientOperator<TOutputImage>
::GenerateInputRequestedRegion()
{
  // Input 0 is the volume in which we backproject
  typename Superclass::InputImagePointer inputPtr0 =
    const_cast< TOutputImage * >( this->GetInput(0) );
  if ( !inputPtr0 ) return;
  inputPtr0->SetRequestedRegion( this->GetOutput()->GetRequestedRegion() );

  // Input 1 is the stack of projections to backproject
  typename Superclass::InputImagePointer  inputPtr1 =
    const_cast< TOutputImage * >( this->GetInput(1) );
  if ( !inputPtr1 ) return;
  inputPtr1->SetRequestedRegion( inputPtr1->GetLargestPossibleRegion() );

  if (m_IsWeighted)
    {
    this->SetNumberOfRequiredInputs(3);

    // Input 2 is the weights map on projections, if any
    typename Superclass::InputImagePointer  inputPtr2 =
            const_cast< TOutputImage * >( this->GetInput(2) );
    if ( !inputPtr2 )
        return;
    inputPtr2->SetRequestedRegion( inputPtr2->GetLargestPossibleRegion() );

    if (m_Preconditioned)
      {
      this->SetNumberOfRequiredInputs(4);

      // Input 3 is the weights map on volumes, if any
      typename Superclass::InputImagePointer  inputPtr3 =
              const_cast< TOutputImage * >( this->GetInput(3) );
      if ( !inputPtr3 )
          return;
      inputPtr3->SetRequestedRegion( inputPtr3->GetLargestPossibleRegion() );
      }
    }
}

template< typename TOutputImage >
void
ReconstructionConjugateGradientOperator<TOutputImage>
::GenerateOutputInformation()
{
  // Set runtime connections, and connections with
  // forward and back projection filters, which are set
  // at runtime
  m_ForwardProjectionFilter->SetInput(0, m_ConstantProjectionsSource->GetOutput());
  m_BackProjectionFilter->SetInput(0, m_ConstantVolumeSource->GetOutput());
  m_DisplacedDetectorFilter->SetInput( m_ForwardProjectionFilter->GetOutput());
  m_ConstantVolumeSource->SetInformationFromImage(this->GetInput(0));
  m_ConstantProjectionsSource->SetInformationFromImage(this->GetInput(1));
  m_ForwardProjectionFilter->SetInput(1, this->GetInput(0));

  if (m_IsWeighted)
    {
    // Multiply the projections
    m_MultiplyProjectionsFilter->SetInput1(m_DisplacedDetectorFilter->GetOutput());
    m_MultiplyProjectionsFilter->SetInput2(this->GetInput(2));
    m_BackProjectionFilter->SetInput(1, m_MultiplyProjectionsFilter->GetOutput());

    if (m_Preconditioned)
      {
      // Multiply the input volume
      m_MultiplyInputVolumeFilter->SetInput1( this->GetInput(0) );
      m_MultiplyInputVolumeFilter->SetInput2( this->GetInput(3) );
      m_ForwardProjectionFilter->SetInput(1, m_MultiplyInputVolumeFilter->GetOutput());

      // Multiply the volume
      m_MultiplyOutputVolumeFilter->SetInput1(m_BackProjectionFilter->GetOutput());
      m_MultiplyOutputVolumeFilter->SetInput2(this->GetInput(3));
      }
    }
  else
    {
    m_BackProjectionFilter->SetInput(1, m_DisplacedDetectorFilter->GetOutput());
    }

  // Set geometry
  m_ForwardProjectionFilter->SetGeometry(this->m_Geometry);
  m_BackProjectionFilter->SetGeometry(this->m_Geometry.GetPointer());
  m_DisplacedDetectorFilter->SetGeometry(this->m_Geometry);

  // Set memory management parameters for forward
  // and back projection filters
  m_ForwardProjectionFilter->ReleaseDataFlagOn();
  m_DisplacedDetectorFilter->ReleaseDataFlagOn();

  // Have the last filter calculate its output information
  m_BackProjectionFilter->UpdateOutputInformation();

  // Copy it as the output information of the composite filter
  this->GetOutput()->CopyInformation( m_BackProjectionFilter->GetOutput() );
}

template< typename TOutputImage >
void ReconstructionConjugateGradientOperator<TOutputImage>::GenerateData()
{
  // Execute Pipeline
  if (m_Preconditioned)
    {
    m_MultiplyOutputVolumeFilter->Update();
    this->GraftOutput( m_MultiplyOutputVolumeFilter->GetOutput() );
    }
  else
    {
    m_BackProjectionFilter->Update();
    this->GraftOutput( m_BackProjectionFilter->GetOutput() );
    }
  // Get the output
}

}// end namespace


#endif
