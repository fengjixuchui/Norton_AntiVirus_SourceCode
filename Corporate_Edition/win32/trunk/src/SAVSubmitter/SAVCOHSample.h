// /////////////////////////////////////////////////////////////////////////////////////////////////
// Symantec copyright header start
// //////////////////////
//
// PROPRIETARY / CONFIDENTIAL.
// Use of this product is subject to license terms.
// Copyright � 2006 Symantec Corporation.
// All rights reserved.
//
// /////////////////////////////////////////////////////////////////////////////////////////////////
// Symantec copyright header stop
// //////////////////////
#pragma once

#include "SubmissionBase.h"
#include "Allocator.h"

class CEraserInterfaceLoader;

namespace SAVSubmission {

class CSAVCOHSample :
	public CSubmissionBase,
	public mem::CAllocator<CSAVCOHSample, ccScanw::IScannerw*>
{
public:
	/* When constructing a submission we need the scanner. But when we're deserializing the submission
	* within the Submission Engine we need to support creating an instance using the default constructor
	* in which case a scanner is not needed.
	*/
	CSAVCOHSample();
	CSAVCOHSample(ccScanw::IScannerw* pScanner);

	virtual ~CSAVCOHSample(void);

	virtual HRESULT GetTypeId(GUID& typeId) const throw();
	virtual bool GetObjectId(SYMOBJECT_ID&) const throw();
	virtual HRESULT Initialize(const ccEraser::IAnomaly* pAnomaly) throw();

	//IAVSubmission
	virtual HRESULT GetAnomaly(ccEraser::IAnomaly*& pAnomaly) const throw();
	virtual HRESULT GetRemediationCount(size_t& nSize) const throw();
	virtual HRESULT GetRemediation(size_t nIndex, ccEraser::IRemediationAction*& pRemediation, cc::IStream*& pStream) const throw();

	//ccSym::CSerialize
	virtual bool Save(ccLib::CArchive&) const throw();
	virtual bool Load(ccLib::CArchive&) throw();

protected:
	HRESULT GetEraserInstance(ccEraser::IEraser*& pEraser);
	ccEraser::IAnomaly* LoadAnomalyFromStream(cc::IStream* pStream);

private:
	CSAVCOHSample(const CSAVCOHSample&);
	const CSAVCOHSample& operator = (const CSAVCOHSample&);
	bool operator== (const CSAVCOHSample&);

	static const DWORD kdwVersion = 0x1;

	//TODO:JJM: we have lifetime, and multiple use problems with this currently. Needs to be moved out to another class.
	CEraserInterfaceLoader*	m_pEraserLoader;

	ccEraser::IAnomalyPtr m_ptrAnomaly;
};

// This comes from the consumer team's AVSubmit project.
// TODO:JJM: It really should be put in a header file of the SubSDK.
DEFINE_GUID(TYPEID_AV_DETECTION, 0x6e70c46b, 0x7fd6, 0x4d71, 0xa2, 0x33, 0xb1, 0x1, 0xe9, 0xad, 0x72, 0xf2);

} //namespace SAVSubmission