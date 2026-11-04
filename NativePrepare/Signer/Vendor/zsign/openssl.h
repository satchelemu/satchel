#pragma once
#include "json.h"

class ZSignAsset
{
public:
	ZSignAsset();
	~ZSignAsset();
	bool InitMemory(const void* data, size_t length, const char* password, const char* expectedTeam);

public:
	bool Init(const string& strCertFile, 
				const string& strPKeyFile,
				const string& strProvFile,
				const string& strEntitleFile,
				const string& strPassword,
				bool bAdhoc,
				bool bSHA256Only,
				bool bSingleBinary);

	bool GenerateCMS(const string& strCDHashData, 
						const string& strCDHashesPlist, 
						const string& strCodeDirectorySlotSHA1, 
						const string& strAltnateCodeDirectorySlot256, 
						string& strCMSOutput);

private:
	bool GenerateCMS(void* pscert, 
						void* pspkey, 
						const string& strCDHashData, 
						const string& strCDHashesPlist, 
						const string& strCodeDirectorySlotSHA1, 
						const string& strAltnateCodeDirectorySlot256, 
						string& strCMSOutput);

	bool GetCertSubjectCN(void* cert, string& strSubjectCN);
	bool GetCertSubjectCN(const string& strCertData, string& strSubjectCN);

public:
	static bool		CMSError();
	// Returns the embedded WWDR intermediate (G1-G8) whose subject name hash
	// matches uIssuerHash, or NULL. Shared by signing and certificate check.
	static const char*	WWDRIntermediatePEM(unsigned long uIssuerHash);
	static void*	GenerateASN1Type(const string& value);
	static bool		GetCertInfo(void* pcert, jvalue& jvCertInfo);
	static bool		GetCMSInfo(uint8_t* pCMSData, uint32_t uCMSLength, jvalue& jvOutput);
	static bool		GetCMSContent(const string& strCMSDataInput, string& strContentOutput);
	static void		ParseCertSubject(const string& strSubject, jvalue& jvSubject);
	static string	ASN1_TIMEtoString(const void* time);

public:
	// Probe matrix: 0 omits entitlements, 1 writes XML, 2 writes XML and DER.
    unsigned m_dylibEntitlements = 1;
    uint8_t m_uPageSize = 12;
	bool	m_bAdhoc;
	bool	m_bSHA256Only;
	bool	m_bSingleBinary;
	string	m_strTeamId;
	string m_selfSignedLeafSHA1;
	string	m_strSubjectCN;
	string	m_strProvData;
	string	m_strEntitleData;
	string	m_strApplicationId;

private:
	void*	m_evpPKey;
	void*	m_x509Cert;
	void*	m_caCerts; // STACK_OF(X509)* CA chain recovered from the input p12, if any

public:
	static const char* s_szAppleDevCACert;
	static const char* s_szAppleRootCACert;
	static const char* s_szAppleRootCACertG3;
	static const char* s_szAppleDevCACertG3;
	static const char* s_szAppleDevCACertG2;
	static const char* s_szAppleDevCACertG4;
	static const char* s_szAppleDevCACertG5;
	static const char* s_szAppleDevCACertG6;
	static const char* s_szAppleDevCACertG7;
	static const char* s_szAppleDevCACertG8;

public:
	class OpenSSLInit
	{
	public:
		OpenSSLInit();
	};
	static OpenSSLInit s_OpenSSLInit;
};
