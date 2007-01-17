/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License (the "License").
 * You may not use this file except in compliance with the License.
 *
 * You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
 * or http://www.opensolaris.org/os/licensing.
 * See the License for the specific language governing permissions
 * and limitations under the License.
 *
 * When distributing Covered Code, include this CDDL HEADER in each
 * file and include the License file at usr/src/OPENSOLARIS.LICENSE.
 * If applicable, add the following below this CDDL HEADER, with the
 * fields enclosed by brackets "[]" replaced with your own identifying
 * information: Portions Copyright [yyyy] [name of copyright owner]
 *
 * CDDL HEADER END
 */
/*
 * Copyright 2007 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#pragma ident	"%Z%%M%	%I%	%E% SMI"

#include <stdio.h>
#include <link.h>
#include <fcntl.h>
#include <ctype.h>
#include <sys/param.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/socket.h>

#include <ber_der.h>
#include <kmfapiP.h>

#include <pem_encode.h>
#include <libgen.h>
#include <cryptoutil.h>


/*
 *
 * Name: KMF_SetCSRPubKey
 *
 * Description:
 *   This function converts the specified plugin public key to SPKI form,
 *   and save it in the KMF_CSR_DATA internal structure
 *
 * Parameters:
 *   KMFkey(input) - pointer to the KMF_KEY_HANDLE structure containing the
 *		public key generated by the plug-in CreateKeypair
 *   Csr(input/output) - pointer to a KMF_CSR_DATA structure containing
 *		SPKI
 *
 * Returns:
 *   A KMF_RETURN value indicating success or specifying a particular
 *   error condition.
 *   The value KMF_OK indicates success. All other values represent
 *   an error condition.
 *
 */
KMF_RETURN
KMF_SetCSRPubKey(KMF_HANDLE_T handle,
	KMF_KEY_HANDLE *KMFKey,
	KMF_CSR_DATA *Csr)
{
	KMF_RETURN ret = KMF_OK;
	KMF_X509_SPKI *spki_ptr;
	KMF_PLUGIN *plugin;
	KMF_DATA KeyData = {NULL, 0};

	CLEAR_ERROR(handle, ret);
	if (ret != KMF_OK)
		return (ret);

	if (KMFKey == NULL || Csr == NULL) {
		return (KMF_ERR_BAD_PARAMETER);
	}

	/* The keystore must extract the pubkey data */
	plugin = FindPlugin(handle, KMFKey->kstype);
	if (plugin != NULL && plugin->funclist->EncodePubkeyData != NULL) {
		ret = plugin->funclist->EncodePubkeyData(handle,
		    KMFKey, &KeyData);
	} else {
		return (KMF_ERR_PLUGIN_NOTFOUND);
	}

	spki_ptr = &Csr->csr.subjectPublicKeyInfo;

	ret = DerDecodeSPKI(&KeyData, spki_ptr);

	KMF_FreeData(&KeyData);

	return (ret);
}

KMF_RETURN
KMF_SetCSRVersion(KMF_CSR_DATA *CsrData, uint32_t version)
{
	if (CsrData == NULL)
		return (KMF_ERR_BAD_PARAMETER);

	/*
	 * From RFC 3280:
	 * Version  ::=  INTEGER  {  v1(0), v2(1), v3(2)  }
	 */
	if (version != 0 && version != 1 && version != 2)
		return (KMF_ERR_BAD_PARAMETER);
	return (set_integer(&CsrData->csr.version, (void *)&version,
		sizeof (uint32_t)));
}

KMF_RETURN
KMF_SetCSRSubjectName(KMF_CSR_DATA *CsrData,
	KMF_X509_NAME *subject_name_ptr)
{
	if (CsrData != NULL && subject_name_ptr != NULL)
		CsrData->csr.subject = *subject_name_ptr;
	else
		return (KMF_ERR_BAD_PARAMETER);

	return (KMF_OK);
}

KMF_RETURN
KMF_CreateCSRFile(KMF_DATA *csrdata, KMF_ENCODE_FORMAT format,
	char *csrfile)
{
	KMF_RETURN rv = KMF_OK;
	int fd = -1;
	KMF_DATA pemdata = {NULL, 0};

	if (csrdata == NULL || csrfile == NULL)
		return (KMF_ERR_BAD_PARAMETER);

	if (format != KMF_FORMAT_PEM && format != KMF_FORMAT_ASN1)
		return (KMF_ERR_BAD_PARAMETER);

	if (format == KMF_FORMAT_PEM) {
		int len;
		rv = KMF_Der2Pem(KMF_CSR,
			csrdata->Data, csrdata->Length,
			&pemdata.Data, &len);
		if (rv != KMF_OK)
			goto cleanup;
		pemdata.Length = (size_t)len;
	}

	if ((fd = open(csrfile, O_CREAT |O_RDWR, 0644)) == -1) {
		rv = KMF_ERR_OPEN_FILE;
		goto cleanup;
	}

	if (format == KMF_FORMAT_PEM) {
		if (write(fd, pemdata.Data, pemdata.Length) !=
			pemdata.Length) {
			rv = KMF_ERR_WRITE_FILE;
		}
	} else {
		if (write(fd, csrdata->Data, csrdata->Length) !=
			csrdata->Length) {
			rv = KMF_ERR_WRITE_FILE;
		}
	}

cleanup:
	if (fd != -1)
		(void) close(fd);

	KMF_FreeData(&pemdata);

	return (rv);
}

KMF_RETURN
KMF_SetCSRExtension(KMF_CSR_DATA *Csr,
	KMF_X509_EXTENSION *extn)
{
	KMF_RETURN ret = KMF_OK;
	KMF_X509_EXTENSIONS *exts;

	if (Csr == NULL || extn == NULL)
		return (KMF_ERR_BAD_PARAMETER);

	exts = &Csr->csr.extensions;

	ret = add_an_extension(exts, extn);

	return (ret);
}

KMF_RETURN
KMF_SetCSRSignatureAlgorithm(KMF_CSR_DATA *CsrData,
	KMF_ALGORITHM_INDEX sigAlg)
{
	KMF_OID	*alg;

	if (CsrData == NULL)
		return (KMF_ERR_BAD_PARAMETER);

	alg = X509_AlgIdToAlgorithmOid(sigAlg);

	if (alg != NULL) {
		(void) copy_data((KMF_DATA *)
			&CsrData->signature.algorithmIdentifier.algorithm,
			(KMF_DATA *)alg);
		(void) copy_data(
		    &CsrData->signature.algorithmIdentifier.parameters,
		    &CsrData->csr.subjectPublicKeyInfo.algorithm.parameters);
	} else {
		return (KMF_ERR_BAD_PARAMETER);
	}
	return (KMF_OK);
}

KMF_RETURN
KMF_SetCSRSubjectAltName(KMF_CSR_DATA *Csr,
	char *altname, int critical,
	KMF_GENERALNAMECHOICES alttype)
{
	KMF_RETURN ret = KMF_OK;

	if (Csr == NULL || altname == NULL)
		return (KMF_ERR_BAD_PARAMETER);

	ret = KMF_SetAltName(&Csr->csr.extensions,
		(KMF_OID *)&KMFOID_SubjectAltName, critical, alttype,
		altname);

	return (ret);
}

KMF_RETURN
KMF_SetCSRKeyUsage(KMF_CSR_DATA *CSRData,
	int critical, uint16_t kubits)
{
	KMF_RETURN ret = KMF_OK;

	if (CSRData == NULL)
		return (KMF_ERR_BAD_PARAMETER);

	ret = set_key_usage_extension(
		&CSRData->csr.extensions,
		critical, kubits);

	return (ret);
}

/*
 *
 * Name: KMF_SignCSR
 *
 * Description:
 *   This function signs a CSR and returns the result as a
 *   signed, encoded CSR in SignedCsr
 *
 * Parameters:
 *   tbsCsr(input) - pointer to a KMF_DATA structure containing a
 *		DER encoded TBS CSR data
 *   Signkey(input) - pointer to the KMF_KEY_HANDLE structure containing
 *		the private key generated by the plug-in CreateKeypair
 *   algo(input) - contains algorithm info needed for signing
 *   SignedCsr(output) - pointer to the KMF_DATA structure containing
 *		the signed CSR
 *
 * Returns:
 *   A KMF_RETURN value indicating success or specifying a particular
 *   error condition.
 *   The value KMF_OK indicates success. All other values represent
 *   an error condition.
 *
 */
KMF_RETURN
KMF_SignCSR(KMF_HANDLE_T handle,
	const KMF_CSR_DATA *tbsCsr,
	KMF_KEY_HANDLE	*Signkey,
	KMF_DATA	*SignedCsr)
{
	KMF_RETURN err;
	KMF_DATA csrdata = { NULL, 0 };

	CLEAR_ERROR(handle, err);
	if (err != KMF_OK)
		return (err);

	if (tbsCsr == NULL ||
		Signkey == NULL || SignedCsr == NULL)
		return (KMF_ERR_BAD_PARAMETER);

	SignedCsr->Data = NULL;
	SignedCsr->Length = 0;

	err = DerEncodeTbsCsr((KMF_TBS_CSR *)&tbsCsr->csr, &csrdata);
	if (err == KMF_OK) {
		err = SignCsr(handle, &csrdata, Signkey,
			(KMF_X509_ALGORITHM_IDENTIFIER *)
				&tbsCsr->signature.algorithmIdentifier,
			SignedCsr);
	}

	if (err != KMF_OK) {
		KMF_FreeData(SignedCsr);
	}
	KMF_FreeData(&csrdata);
	return (err);
}

KMF_RETURN
KMF_ImportCRL(KMF_HANDLE_T handle, KMF_IMPORTCRL_PARAMS *params)
{
	KMF_PLUGIN *plugin;
	KMF_RETURN ret;

	CLEAR_ERROR(handle, ret);
	if (ret != KMF_OK)
		return (ret);

	if (params == NULL)
		return (KMF_ERR_BAD_PARAMETER);

	switch (params->kstype) {
	case KMF_KEYSTORE_NSS:
		plugin = FindPlugin(handle, params->kstype);
		break;

	case KMF_KEYSTORE_OPENSSL:
	case KMF_KEYSTORE_PK11TOKEN: /* PKCS#11 CRL is file-based */
		plugin = FindPlugin(handle, KMF_KEYSTORE_OPENSSL);
		break;
	default:
		return (KMF_ERR_PLUGIN_NOTFOUND);
	}

	if (plugin != NULL && plugin->funclist->ImportCRL != NULL) {
		return (plugin->funclist->ImportCRL(handle, params));
	}
	return (KMF_ERR_PLUGIN_NOTFOUND);
}

KMF_RETURN
KMF_DeleteCRL(KMF_HANDLE_T handle, KMF_DELETECRL_PARAMS *params)
{
	KMF_PLUGIN *plugin;
	KMF_RETURN ret;

	CLEAR_ERROR(handle, ret);
	if (ret != KMF_OK)
		return (ret);

	if (params == NULL)
		return (KMF_ERR_BAD_PARAMETER);

	switch (params->kstype) {
	case KMF_KEYSTORE_NSS:
		plugin = FindPlugin(handle, params->kstype);
		break;

	case KMF_KEYSTORE_OPENSSL:
	case KMF_KEYSTORE_PK11TOKEN: /* PKCS#11 CRL is file-based */
		plugin = FindPlugin(handle, KMF_KEYSTORE_OPENSSL);
		break;
	default:
		return (KMF_ERR_PLUGIN_NOTFOUND);
	}

	if (plugin != NULL && plugin->funclist->DeleteCRL != NULL) {
		return (plugin->funclist->DeleteCRL(handle, params));
	} else {
		return (KMF_ERR_PLUGIN_NOTFOUND);
	}
}

KMF_RETURN
KMF_ListCRL(KMF_HANDLE_T handle, KMF_LISTCRL_PARAMS *params, char **crldata)
{
	KMF_PLUGIN *plugin;
	KMF_RETURN ret;

	CLEAR_ERROR(handle, ret);
	if (ret != KMF_OK)
		return (ret);

	if (params == NULL || crldata == NULL)
		return (KMF_ERR_BAD_PARAMETER);

	switch (params->kstype) {
	case KMF_KEYSTORE_NSS:
		plugin = FindPlugin(handle, params->kstype);
		break;

	case KMF_KEYSTORE_OPENSSL:
	case KMF_KEYSTORE_PK11TOKEN: /* PKCS#11 CRL is file-based */
		plugin = FindPlugin(handle, KMF_KEYSTORE_OPENSSL);
		break;
	default:
		return (KMF_ERR_PLUGIN_NOTFOUND);
	}

	if (plugin != NULL && plugin->funclist->ListCRL != NULL) {
		return (plugin->funclist->ListCRL(handle, params, crldata));
	} else {
		return (KMF_ERR_PLUGIN_NOTFOUND);
	}
}

KMF_RETURN
KMF_FindCRL(KMF_HANDLE_T handle, KMF_FINDCRL_PARAMS *params,
	char **CRLNameList, int *CRLCount)
{
	KMF_PLUGIN *plugin;
	KMF_RETURN ret;

	CLEAR_ERROR(handle, ret);
	if (ret != KMF_OK)
		return (ret);

	if (params == NULL ||
		CRLCount == NULL)
		return (KMF_ERR_BAD_PARAMETER);

	plugin = FindPlugin(handle, params->kstype);
	if (plugin != NULL && plugin->funclist->FindCRL != NULL) {
		return (plugin->funclist->FindCRL(handle, params,
			CRLNameList, CRLCount));
	} else {
		return (KMF_ERR_PLUGIN_NOTFOUND);
	}
}

KMF_RETURN
KMF_FindCertInCRL(KMF_HANDLE_T handle, KMF_FINDCERTINCRL_PARAMS *params)
{
	KMF_PLUGIN *plugin;
	KMF_RETURN ret;

	CLEAR_ERROR(handle, ret);
	if (ret != KMF_OK)
		return (ret);

	if (params == NULL)
		return (KMF_ERR_BAD_PARAMETER);

	switch (params->kstype) {
	case KMF_KEYSTORE_NSS:
		plugin = FindPlugin(handle, params->kstype);
		break;

	case KMF_KEYSTORE_OPENSSL:
	case KMF_KEYSTORE_PK11TOKEN: /* PKCS#11 CRL is file-based */
		plugin = FindPlugin(handle, KMF_KEYSTORE_OPENSSL);
		break;
	default:
		return (KMF_ERR_PLUGIN_NOTFOUND);
	}

	if (plugin != NULL && plugin->funclist->FindCertInCRL != NULL) {
		return (plugin->funclist->FindCertInCRL(handle, params));
	} else {
		return (KMF_ERR_PLUGIN_NOTFOUND);
	}
}

KMF_RETURN
KMF_VerifyCRLFile(KMF_HANDLE_T handle,
	KMF_VERIFYCRL_PARAMS *params)
{
	KMF_PLUGIN *plugin;
	KMF_RETURN (*verifyCRLFile)(KMF_HANDLE_T,
		KMF_VERIFYCRL_PARAMS *);
	KMF_RETURN ret;

	CLEAR_ERROR(handle, ret);
	if (ret != KMF_OK)
		return (ret);

	if (params == NULL)
		return (KMF_ERR_BAD_PARAMETER);

	plugin = FindPlugin(handle, KMF_KEYSTORE_OPENSSL);
	if (plugin == NULL || plugin->dldesc == NULL) {
		return (KMF_ERR_PLUGIN_NOTFOUND);
	}

	verifyCRLFile = (KMF_RETURN(*)())dlsym(plugin->dldesc,
	    "OpenSSL_VerifyCRLFile");

	if (verifyCRLFile == NULL) {
		return (KMF_ERR_FUNCTION_NOT_FOUND);
	}

	return (verifyCRLFile(handle, params));
}

KMF_RETURN
KMF_CheckCRLDate(KMF_HANDLE_T handle, KMF_CHECKCRLDATE_PARAMS *params)
{
	KMF_PLUGIN *plugin;
	KMF_RETURN (*checkCRLDate)(void *,
	    KMF_CHECKCRLDATE_PARAMS *params);
	KMF_RETURN ret;

	CLEAR_ERROR(handle, ret);
	if (ret != KMF_OK)
		return (ret);

	if (params == NULL)
		return (KMF_ERR_BAD_PARAMETER);

	plugin = FindPlugin(handle, KMF_KEYSTORE_OPENSSL);
	if (plugin == NULL || plugin->dldesc == NULL) {
		return (KMF_ERR_PLUGIN_NOTFOUND);
	}

	checkCRLDate = (KMF_RETURN(*)())dlsym(plugin->dldesc,
	    "OpenSSL_CheckCRLDate");

	if (checkCRLDate == NULL) {
		return (KMF_ERR_FUNCTION_NOT_FOUND);
	}

	return (checkCRLDate(handle, params));

}

KMF_RETURN
KMF_IsCRLFile(KMF_HANDLE_T handle, char *filename, KMF_ENCODE_FORMAT *pformat)
{
	KMF_PLUGIN *plugin;
	KMF_RETURN (*IsCRLFileFn)(void *, char *, KMF_ENCODE_FORMAT *);
	KMF_RETURN ret;

	CLEAR_ERROR(handle, ret);
	if (ret != KMF_OK)
		return (ret);

	if (filename  == NULL || pformat == NULL) {
		return (KMF_ERR_BAD_PARAMETER);
	}

	/*
	 * This framework function is actually implemented in the openssl
	 * plugin library, so we find the function address and call it.
	 */
	plugin = FindPlugin(handle, KMF_KEYSTORE_OPENSSL);
	if (plugin == NULL || plugin->dldesc == NULL) {
		return (KMF_ERR_PLUGIN_NOTFOUND);
	}

	IsCRLFileFn = (KMF_RETURN(*)())dlsym(plugin->dldesc,
	    "OpenSSL_IsCRLFile");
	if (IsCRLFileFn == NULL) {
		return (KMF_ERR_FUNCTION_NOT_FOUND);
	}

	return (IsCRLFileFn(handle, filename, pformat));
}
