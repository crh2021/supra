// ================================================================================================
// 
// If not explicitly stated: Copyright (C) 2019, all rights reserved,
//      Walter Simson 
//      Rüdiger Göbl 
//		Email r.goebl@tum.de
//      Chair for Computer Aided Medical Procedures
//      Technische Universität München
//      Boltzmannstr. 3, 85748 Garching b. München, Germany
// 
// ================================================================================================

#include "FilterSradCudaNode.h"
#include "FilterSradCuda.h"

#include "USImage.h"
#include <utilities/Logging.h>

using namespace std;

namespace supra
{
	FilterSradCudaNode::FilterSradCudaNode(tbb::flow::graph & graph, const std::string & nodeID, bool queueing)
		: AbstractNode(nodeID, queueing)
	{
		// Create the underlying tbb node for handling the message passing. This usually does not need to be modified.
		if (queueing)
		{
			m_node = unique_ptr<NodeTypeQueueing>(
				new NodeTypeQueueing(graph, 1, [this](shared_ptr<RecordObject> inObj) -> shared_ptr<RecordObject> { return checkTypeAndProcess(inObj); }));
		}
		else
		{
			m_node = unique_ptr<NodeTypeDiscarding>(
				new NodeTypeDiscarding(graph, 1, [this](shared_ptr<RecordObject> inObj) -> shared_ptr<RecordObject> { return checkTypeAndProcess(inObj); }));
		}

		m_callFrequency.setName("FilterSradCudaNode");

		// Define the parameters that this node reveals to the user
		m_valueRangeDictionary.set<uint32_t>("numberIterations", 0, 1000, 300, "Number of Iterations");
		m_valueRangeDictionary.set<double>("lambda", 0.0, 2.0, 1.0, "Step Size Lambda");
		m_valueRangeDictionary.set<double>("speckleScale", 0.0, 2.0, 1.0, "Speckle Scale");
		m_valueRangeDictionary.set<double>("speckleScaleDecay", 0.0, 2.0, 1.0 / 6.0, "Speckle Scale Decay (rho)");
		m_valueRangeDictionary.set<DataType>("outputType", { TypeFloat, TypeUint8, TypeInt16 }, TypeFloat, "Output type");

		// read the configuration to apply the default values
		configurationChanged();
	}

	void FilterSradCudaNode::configurationChanged()
	{
		m_numberIterations = m_configurationDictionary.get<uint32_t>("numberIterations");
		m_lambda = m_configurationDictionary.get<double>("lambda");
		m_speckleScale = m_configurationDictionary.get<double>("speckleScale");
		m_speckleScaleDecay = m_configurationDictionary.get<double>("speckleScaleDecay");
		m_outputType = m_configurationDictionary.get<DataType>("outputType");
	}

	void FilterSradCudaNode::configurationEntryChanged(const std::string& configKey)
	{
		// lock the object mutex to make sure no processing happens during parameter changes
		unique_lock<mutex> l(m_mutex);
		if (configKey == "numberIterations")
		{
			m_numberIterations = m_configurationDictionary.get<uint32_t>("numberIterations");
		}
		else if (configKey == "lambda")
		{
			m_lambda = m_configurationDictionary.get<double>("lambda");
		}
		else if (configKey == "speckleScale")
		{
			m_speckleScale = m_configurationDictionary.get<double>("speckleScale");
		}
		else if (configKey == "speckleScaleDecay")
		{
			m_speckleScaleDecay = m_configurationDictionary.get<double>("speckleScaleDecay");
		}
		else if (configKey == "outputType")
		{
			m_outputType = m_configurationDictionary.get<DataType>("outputType");
		}
	}

	template <typename InputType>
	std::shared_ptr<ContainerBase> FilterSradCudaNode::processTemplateSelection(std::shared_ptr<const Container<InputType> > imageData, vec3s size)
	{
		// With the function already templated on the input type, handle the desired output type.
		switch (m_outputType)
		{
		case supra::TypeUint8:
			return FilterSradCuda::process<InputType, uint8_t>(imageData, size, m_eps, m_numberIterations, m_lambda, m_speckleScale, m_speckleScaleDecay);
			break;
		case supra::TypeInt16:
			return FilterSradCuda::process<InputType, int16_t>(imageData, size, m_eps, m_numberIterations, m_lambda, m_speckleScale, m_speckleScaleDecay);
			break;
		case supra::TypeFloat:
			return FilterSradCuda::process<InputType, float>(imageData, size, m_eps, m_numberIterations, m_lambda, m_speckleScale, m_speckleScaleDecay);
			break;
		default:
			logging::log_error("FilterSradCudaNode: Output image type not supported");
			break;
		}
		return nullptr;
	}

	shared_ptr<RecordObject> FilterSradCudaNode::checkTypeAndProcess(shared_ptr<RecordObject> inObj)
	{
		shared_ptr<USImage> pImage = nullptr;
		if (inObj && inObj->getType() == TypeUSImage)
		{
			shared_ptr<USImage> pInImage = dynamic_pointer_cast<USImage>(inObj);
			if (pInImage)
			{
				// lock the object mutex to make sure no parameters are changed during processing
				unique_lock<mutex> l(m_mutex);
				m_callFrequency.measure();

				std::shared_ptr<ContainerBase> pImageProcessedData;

				// The input and output types have to be determined dynamically. We do this in to stages of templated functions.
				// This first switch handles the different input data types. There is no need to support all types, 
				// only those meaningful for the operation of the node.
				switch (pInImage->getDataType())
				{
				case TypeUint8:
					pImageProcessedData = processTemplateSelection<uint8_t>(pInImage->getData<uint8_t>(), pInImage->getSize());
					break;
				case TypeInt16:
					pImageProcessedData = processTemplateSelection<int16_t>(pInImage->getData<int16_t>(), pInImage->getSize());
					break;
				case TypeFloat:
					pImageProcessedData = processTemplateSelection<float>(pInImage->getData<float>(), pInImage->getSize());
					break;
				default:
					logging::log_error("FilterSradCudaNode: Input image type not supported");
					break;
				}
				m_callFrequency.measureEnd();

				// Wrap the returned Container in an USImage with the same size etc.
				pImage = make_shared<USImage>(
					pInImage->getSize(),
					pImageProcessedData,
					pInImage->getImageProperties(),
					pInImage->getReceiveTimestamp(),
					pInImage->getSyncTimestamp());
			}
			else {
				logging::log_error("FilterSradCudaNode: could not cast object to USImage type, is it in suppored ElementType?");
			}
		}
		return pImage;
	}
}