/*
 * Copyright © 2012-2015 VMware, Inc.  All Rights Reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the “License”); you may not
 * use this file except in compliance with the License.  You may obtain a copy
 * of the License at http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an “AS IS” BASIS, without
 * warranties or conditions of any kind, EITHER EXPRESS OR IMPLIED.  See the
 * License for the specific language governing permissions and limitations
 * under the License.
 */



/*
 * Module Name: Replicate entries
 *
 * Filename: replentry.c
 *
 * Abstract:
 *
 */

#include "includes.h"

static
int
SetupReplModifyRequest(
    PVDIR_OPERATION    modOp,
    PVDIR_ENTRY        pEntry
    );

static
int
_VmDirPatchData(
    PVDIR_OPERATION     pOperation
    );

static
int
ReplFixUpEntryDn(
    PVDIR_ENTRY pEntry
    );

static
int
_VmDirDetatchValueMetaData(
    PVDIR_OPERATION     pOperation,
    PVDIR_ENTRY         pEntry,
    PVDIR_ATTRIBUTE *   ppAttrAttrValueMetaData
    );

static
int
_VmDirAttachValueMetaData(
    PVDIR_ATTRIBUTE pAttrAttrValueMetaData,
    PVDIR_ENTRY     pEntry,
    USN             localUsn
    );

static
int
_VmDeleteOldValueMetaData(
    PVDIR_OPERATION     pModOp,
    PVDIR_MODIFICATION  pMods,
    ENTRYID             entryId
    );

static
int
_VmSetupValueMetaData(
    PVDIR_SCHEMA_CTX    pSchemaCtx,
    PVDIR_OPERATION     pModOp,
    PVDIR_ATTRIBUTE     pAttrAttrValueMetaData,
    USN                 localUsn,
    ENTRYID             entryId
    );

static
int
_VmDirAttrValueMetaResolve(
    PVDIR_OPERATION pModOp,
    PVDIR_ATTRIBUTE pAttr,
    PVDIR_BERVALUE  suppAttrMetaValue,
    ENTRYID         entryId,
    PBOOLEAN        pInScope
    );

static
VOID
_VmDirLogReplAddEntryContent(
    PVMDIR_REPLICATION_PAGE_ENTRY pPageEntry,
    PVDIR_ENTRY                   pEntry
    );

static
VOID
_VmDirLogReplModifyEntryContent(
    PVMDIR_REPLICATION_PAGE_ENTRY pPageEntry,
    PVDIR_ENTRY                   pEntry
    );

static
VOID
_VmDirLogReplDeleteEntryContent(
    PVMDIR_REPLICATION_PAGE_ENTRY pPageEntry,
    PVDIR_ENTRY                   pEntry
    );

static
VOID
_VmDirLogReplEntryContent(
    PVDIR_ENTRY                   pEntry
    );

static
VOID
_VmDirLogReplModifyModContent(
    ModifyReq*  pModReq
    );

// Replicate Add Entry operation

int
ReplAddEntry(
    PVDIR_SCHEMA_CTX                pSchemaCtx,
    PVMDIR_REPLICATION_PAGE_ENTRY   pPageEntry,
    PVDIR_SCHEMA_CTX*               ppOutSchemaCtx
    )
{
    int                 retVal = LDAP_SUCCESS;
    VDIR_OPERATION      op = {0};
    PVDIR_ATTRIBUTE     pAttr = NULL;
    PVDIR_ENTRY         pEntry = NULL;
    USN                 localUsn = 0;
    PSTR                pszAttrType = NULL;
    PVDIR_ATTRIBUTE     pAttrAttrMetaData = NULL;
    PVDIR_ATTRIBUTE     pAttrAttrValueMetaData = NULL;
    PVDIR_SCHEMA_CTX    pUpdateSchemaCtx = NULL;
    LDAPMessage *       ldapMsg = pPageEntry->entry;
    PLW_HASHMAP         pMetaDataMap = NULL;
    LW_HASHMAP_ITER     iter = LW_HASHMAP_ITER_INIT;
    LW_HASHMAP_PAIR     pair = {NULL, NULL};
    PVMDIR_ATTRIBUTE_METADATA   pSupplierMetaData = NULL;

    retVal = VmDirInitStackOperation( &op,
                                      VDIR_OPERATION_TYPE_REPL,
                                      LDAP_REQ_ADD,
                                      pSchemaCtx );
    BAIL_ON_VMDIR_ERROR(retVal);

    pEntry = op.request.addReq.pEntry;  // init pEntry after VmDirInitStackOperation

    // non-retry case
    if (pPageEntry->pBervEncodedEntry == NULL)
    {
        op.ber = ldapMsg->lm_ber;

        retVal = VmDirParseEntry( &op );
        BAIL_ON_VMDIR_ERROR( retVal );

        pEntry->pSchemaCtx = VmDirSchemaCtxClone(op.pSchemaCtx);

        // Make sure Attribute has its ATDesc set
        retVal = VmDirSchemaCheckSetAttrDesc(pEntry->pSchemaCtx, pEntry);
        BAIL_ON_VMDIR_ERROR(retVal);

        VmDirReplicationEncodeEntryForRetry(pEntry, pPageEntry);
    }
    else // retry case
    {
        // Schema context will be cloned as part of the decode entry (VmDirDecodeEntry)
        retVal = VmDirReplicationDecodeEntryForRetry(op.pSchemaCtx, pPageEntry, pEntry);
        BAIL_ON_VMDIR_ERROR(retVal);

        retVal = VmDirBervalContentDup(&pPageEntry->reqDn, &op.reqDn);
        BAIL_ON_VMDIR_ERROR(retVal);
    }

    _VmDirLogReplAddEntryContent(pPageEntry, op.request.addReq.pEntry);

    op.pBEIF = VmDirBackendSelect(pEntry->dn.lberbv.bv_val);
    assert(op.pBEIF);

    VMDIR_LOG_DEBUG(LDAP_DEBUG_REPL, "ReplAddEntry: next entry being replicated/Added is: %s", pEntry->dn.lberbv.bv_val);

    // Set local attributes.
    if((retVal = VmDirWriteQueuePush(
                    op.pBECtx,
                    gVmDirServerOpsGlobals.pWriteQueue,
                    op.pWriteQueueEle)) != 0)
    {
        VMDIR_LOG_ERROR(
            VMDIR_LOG_MASK_ALL,
            "%s: failed with error code: %d, error string: %s",
            __FUNCTION__,
            retVal,
            VDIR_SAFE_STRING(op.pBEErrorMsg));
        retVal = LDAP_OPERATIONS_ERROR;
        BAIL_ON_VMDIR_ERROR(retVal);
    }

    localUsn = op.pWriteQueueEle->usn;

    VMDIR_LOG_DEBUG(LDAP_DEBUG_REPL, "%s: next generated USN: %" PRId64, __FUNCTION__, localUsn);

    retVal = _VmDirDetatchValueMetaData(&op, pEntry, &pAttrAttrValueMetaData);
    BAIL_ON_VMDIR_ERROR( retVal );

    // need these before DetectAndResolveAttrsConflicts
    op.pszPartner = pPageEntry->pszPartner;
    op.ulPartnerUSN = pPageEntry->ulPartnerUSN;

    retVal = VmDirReplSetAttrNewMetaData(&op, pEntry, &pMetaDataMap);
    BAIL_ON_VMDIR_ERROR(retVal);

    // Creating deleted object scenario: Create attributes just with attribute meta data, and no values.
    while (LwRtlHashMapIterate(pMetaDataMap, &iter, &pair))
    {
        pszAttrType = (PSTR) pair.pKey;
        pSupplierMetaData = (PVMDIR_ATTRIBUTE_METADATA) pair.pValue;

        pair.pKey = pair.pValue = NULL;
        retVal = LwRtlHashMapRemove(pMetaDataMap, pszAttrType, &pair);
        BAIL_ON_VMDIR_ERROR(retVal);

        retVal = VmDirAttributeAllocate(pszAttrType, 0, pEntry->pSchemaCtx, &pAttr);
        BAIL_ON_VMDIR_ERROR(retVal);

        // Set localUsn in the metaData
        retVal = VmDirMetaDataSetLocalUsn(pSupplierMetaData, localUsn);
        BAIL_ON_VMDIR_ERROR(retVal);

        pAttr->pMetaData = pSupplierMetaData;
        pair.pValue = NULL;

        pAttr->next = pEntry->attrs;
        pEntry->attrs = pAttr;

        VmDirFreeMetaDataMapPair(&pair, NULL);
    }

    retVal = _VmDirAttachValueMetaData(pAttrAttrValueMetaData, pEntry, localUsn);
    BAIL_ON_VMDIR_ERROR( retVal );

    retVal = _VmDirPatchData( &op );
    BAIL_ON_VMDIR_ERROR( retVal );

    if ((retVal = VmDirInternalAddEntry( &op )) != LDAP_SUCCESS)
    {
        // Reset retVal to LDAP level error space (for B/C)
        PSZ_METADATA_BUF    pszMetaData = {'\0'};

        retVal = op.ldapResult.errCode;

        //Ignore error - used only for logging
        VmDirMetaDataSerialize(pEntry->attrs->pMetaData, pszMetaData);

        switch (retVal)
        {
            case LDAP_ALREADY_EXISTS:
                VMDIR_LOG_WARNING(VMDIR_LOG_MASK_ALL,
                          "ReplAddEntry/VmDirInternalAddEntry: %d (Object already exists). "
                          "DN: %s, first attribute: %s, it's meta data: '%s' "
                          "NOT resolving this possible replication CONFLICT or initial objects creation scenario. "
                          "For this object, system may not converge. Partner USN %" PRId64,
                          retVal, pEntry->dn.lberbv.bv_val, pEntry->attrs->type.lberbv.bv_val, pszMetaData,
                          pPageEntry->ulPartnerUSN);

                break;

            case LDAP_NO_SUCH_OBJECT:
                VMDIR_LOG_WARNING(LDAP_DEBUG_REPL,
                          "ReplAddEntryVmDirInternalAddEntry: %d (Parent object does not exist). "
                          "DN: %s, first attribute: %s, it's meta data: '%s' "
                          "NOT resolving this possible replication CONFLICT or out-of-parent-child-order replication scenario. "
                          "For this subtree, system may not converge. Partner USN %" PRId64,
                          retVal, pEntry->dn.lberbv.bv_val, pEntry->attrs->type.lberbv.bv_val, pszMetaData,
                          pPageEntry->ulPartnerUSN);
                break;

            default:
                VMDIR_LOG_ERROR(VMDIR_LOG_MASK_ALL,
                          "ReplAddEntry/VmDirInternalAddEntry:  %d (%s). Partner USN %" PRId64,
                          retVal, VDIR_SAFE_STRING( op.ldapResult.pszErrMsg ), pPageEntry->ulPartnerUSN);
                break;
        }
        BAIL_ON_VMDIR_ERROR( retVal );
    }

    if (VmDirStringEndsWith(
            BERVAL_NORM_VAL(pEntry->dn), SCHEMA_NAMING_CONTEXT_DN, FALSE))
    {
        // schema entry updated, refresh replication schema ctx.
        assert( ppOutSchemaCtx );
        retVal = VmDirSchemaCtxAcquire(&pUpdateSchemaCtx);
        BAIL_ON_VMDIR_ERROR(retVal);
        *ppOutSchemaCtx = pUpdateSchemaCtx;

        VmDirSchemaCtxRelease(pSchemaCtx);
    }

cleanup:
    if (pMetaDataMap)
    {
        LwRtlHashMapClear(
                pMetaDataMap,
                VmDirFreeMetaDataMapPair,
                NULL);
        LwRtlFreeHashMap(&pMetaDataMap);
    }
    // pAttrAttrMetaData is local, needs to be freed within the call
    VmDirFreeAttribute( pAttrAttrValueMetaData );
    VmDirFreeAttribute( pAttrAttrMetaData );
    VmDirFreeOperationContent(&op);

    return retVal;

error:
    VmDirSchemaCtxRelease(pUpdateSchemaCtx);

    goto cleanup;
} // Replicate Add Entry operation

/* Replicate Delete Entry operation
 * Set modifications associated with a Delete operation, and pass-in the modifications, with correct attribute meta data
 * set, to InternalDeleteEntry function, which will apply the mods to the existing entry, and move the object to the
 * DeletedObjects container.
 */
int
ReplDeleteEntry(
    PVDIR_SCHEMA_CTX                pSchemaCtx,
    PVMDIR_REPLICATION_PAGE_ENTRY   pPageEntry
    )
{
    int                 retVal = LDAP_SUCCESS;
    VDIR_OPERATION      tmpAddOp = {0};
    VDIR_OPERATION      delOp = {0};
    ModifyReq *         mr = &(delOp.request.modifyReq);
    LDAPMessage *       ldapMsg = pPageEntry->entry;

    retVal = VmDirInitStackOperation( &delOp,
                                      VDIR_OPERATION_TYPE_REPL,
                                      LDAP_REQ_DELETE,
                                      pSchemaCtx );
    BAIL_ON_VMDIR_ERROR(retVal);

    retVal = VmDirInitStackOperation( &tmpAddOp,
                                      VDIR_OPERATION_TYPE_REPL,
                                      LDAP_REQ_ADD,
                                      pSchemaCtx );
    BAIL_ON_VMDIR_ERROR(retVal);

     // non-retry case
    if (pPageEntry->pBervEncodedEntry == NULL)
    {
        tmpAddOp.ber = ldapMsg->lm_ber;

        retVal = VmDirParseEntry( &tmpAddOp );
        BAIL_ON_VMDIR_ERROR( retVal );

       /*
        * Encode entry requires schema description
        * hence perform encode entry after VmDirSchemaCheckSetAttrDesc
        */
        retVal = VmDirSchemaCheckSetAttrDesc(pSchemaCtx, tmpAddOp.request.addReq.pEntry);
        BAIL_ON_VMDIR_ERROR(retVal);

        VmDirReplicationEncodeEntryForRetry(tmpAddOp.request.addReq.pEntry, pPageEntry);
    }
    else // retry case
    {
        // schema context will be cloned as part of the decode entry (VmDirDecodeEntry)
        retVal = VmDirReplicationDecodeEntryForRetry(pSchemaCtx, pPageEntry, tmpAddOp.request.addReq.pEntry);
        BAIL_ON_VMDIR_ERROR(retVal);

        retVal = VmDirBervalContentDup(&pPageEntry->reqDn, &tmpAddOp.reqDn);
        BAIL_ON_VMDIR_ERROR(retVal);
    }

    retVal = ReplFixUpEntryDn(tmpAddOp.request.addReq.pEntry);
    BAIL_ON_VMDIR_ERROR( retVal );

    _VmDirLogReplDeleteEntryContent(pPageEntry, tmpAddOp.request.addReq.pEntry);

    if (VmDirBervalContentDup( &tmpAddOp.reqDn, &mr->dn ) != 0)
    {
        VMDIR_LOG_ERROR(VMDIR_LOG_MASK_ALL, "ReplDeleteEntry: BervalContentDup failed." );
        retVal = LDAP_OPERATIONS_ERROR;
        BAIL_ON_VMDIR_ERROR( retVal );
    }

    delOp.pBEIF = VmDirBackendSelect(mr->dn.lberbv.bv_val);
    assert(delOp.pBEIF);

    // need these before DetectAndResolveAttrsConflicts
    delOp.pszPartner = pPageEntry->pszPartner;
    delOp.ulPartnerUSN = pPageEntry->ulPartnerUSN;

    if((retVal = VmDirWriteQueuePush(
                    delOp.pBECtx,
                    gVmDirServerOpsGlobals.pWriteQueue,
                    delOp.pWriteQueueEle)) != 0)
    {
        VMDIR_LOG_ERROR(
            VMDIR_LOG_MASK_ALL,
            "%s: failed with error code: %d, error string: %s",
            __FUNCTION__,
            retVal,
            VDIR_SAFE_STRING(delOp.pBEErrorMsg));
        retVal = LDAP_OPERATIONS_ERROR;
        BAIL_ON_VMDIR_ERROR(retVal);
    }

    // SJ-TBD: What about if one or more attributes were meanwhile added to the entry? How do we purge them?
    retVal = SetupReplModifyRequest(&delOp, tmpAddOp.request.addReq.pEntry);
    BAIL_ON_VMDIR_ERROR(retVal);

    // SJ-TBD: What happens when DN of the entry has changed in the meanwhile? => conflict resolution.
    // Should objectGuid, instead of DN, be used to uniquely identify an object?
    if ((retVal = VmDirInternalDeleteEntry( &delOp )) != LDAP_SUCCESS)
    {
        PSZ_METADATA_BUF    pszMetaData = {'\0'};

        retVal = delOp.ldapResult.errCode;

        //Ignore error - used only for logging
        VmDirMetaDataSerialize(mr->mods->attr.pMetaData, pszMetaData);
        // If VmDirInternall call failed, reset retVal to LDAP level error space (for B/C)
        switch (retVal)
        {
            case LDAP_NO_SUCH_OBJECT:
                VMDIR_LOG_WARNING(LDAP_DEBUG_REPL,
                          "ReplDeleteEntry/VmDirInternalDeleteEntry: %d (Object does not exist). "
                          "DN: %s, first attribute: %s, it's meta data: '%s'. "
                          "NOT resolving this possible replication CONFLICT. "
                          "For this object, system may not converge. Partner USN %" PRId64,
                          retVal, mr->dn.lberbv.bv_val, mr->mods->attr.type.lberbv.bv_val, pszMetaData,
                          pPageEntry->ulPartnerUSN);
                break;

            case LDAP_NOT_ALLOWED_ON_NONLEAF:
                VMDIR_LOG_WARNING(LDAP_DEBUG_REPL,
                          "ReplDeleteEntry/VmDirInternalDeleteEntry: %d (Operation not allowed on non-leaf). "
                          "DN: %s, first attribute: %s, it's meta data: '%s'. "
                          "NOT resolving this possible replication CONFLICT. "
                          "For this object, system may not converge. Partner USN %" PRId64,
                          retVal, mr->dn.lberbv.bv_val, mr->mods->attr.type.lberbv.bv_val, pszMetaData,
                          pPageEntry->ulPartnerUSN);
                break;

            case LDAP_NO_SUCH_ATTRIBUTE:
                VMDIR_LOG_WARNING(VMDIR_LOG_MASK_ALL,
                          "ReplDeleteEntry/VmDirInternalDeleteEntry: %d (No such attribute). "
                          "DN: %s, first attribute: %s, it's meta data: '%s'. "
                          "NOT resolving this possible replication CONFLICT. "
                          "For this object, system may not converge. Partner USN %" PRId64,
                          retVal, mr->dn.lberbv.bv_val, mr->mods->attr.type.lberbv.bv_val, pszMetaData,
                          pPageEntry->ulPartnerUSN);
                break;

            default:
                VMDIR_LOG_ERROR(VMDIR_LOG_MASK_ALL,
                          "ReplDeleteEntry/InternalDeleteEntry: %d (%s). Partner USN %" PRId64,
                          retVal, VDIR_SAFE_STRING( delOp.ldapResult.pszErrMsg ),pPageEntry->ulPartnerUSN);
                break;
        }
        BAIL_ON_VMDIR_ERROR( retVal );
    }

cleanup:
    VmDirFreeModifyRequest( mr, FALSE );
    VmDirFreeOperationContent(&delOp);
    VmDirFreeOperationContent(&tmpAddOp);

    return retVal;

error:
    goto cleanup;
} // Replicate Delete entry operation

// Replicate Modify Entry operation
int
ReplModifyEntry(
    PVDIR_SCHEMA_CTX                pSchemaCtx,
    PVMDIR_REPLICATION_PAGE_ENTRY   pPageEntry,
    PVDIR_SCHEMA_CTX*               ppOutSchemaCtx
    )
{
    int                 retVal = LDAP_SUCCESS;
    int                 dbRetVal = 0;
    VDIR_OPERATION      modOp = {0};
    ModifyReq *         mr = &(modOp.request.modifyReq);
    BOOLEAN             bHasTxn = FALSE;
    int                 deadLockRetries = 0;
    PVDIR_SCHEMA_CTX    pUpdateSchemaCtx = NULL;
    VDIR_ENTRY          e = {0};
    PVDIR_ATTRIBUTE     pAttrAttrValueMetaData = NULL;
    VDIR_BERVALUE       bvParentDn = VDIR_BERVALUE_INIT;
    ENTRYID             entryId = 0;
    LDAPMessage *       ldapMsg = pPageEntry->entry;

    retVal = VmDirInitStackOperation( &modOp,
                                      VDIR_OPERATION_TYPE_REPL,
                                      LDAP_REQ_MODIFY,
                                      pSchemaCtx );
    BAIL_ON_VMDIR_ERROR(retVal);

    // non-retry case
    if (pPageEntry->pBervEncodedEntry == NULL)
    {
        retVal = VmDirParseBerToEntry(ldapMsg->lm_ber, &e, NULL, NULL);
        BAIL_ON_VMDIR_ERROR( retVal );

        retVal = VmDirGetParentDN(&e.dn, &bvParentDn);
        BAIL_ON_VMDIR_ERROR(retVal);

        // Do not allow modify to tombstone entries
        if (VmDirIsDeletedContainer(bvParentDn.lberbv.bv_val))
        {
            VMDIR_LOG_ERROR(
                VMDIR_LOG_MASK_ALL,
                "%s: Modify Tombstone entries, dn: %s",
                __FUNCTION__,
                e.dn.lberbv.bv_val);

            if (e.attrs != NULL)
            {
                _VmDirLogReplEntryContent(&e);
            }
            BAIL_WITH_VMDIR_ERROR(retVal, LDAP_OPERATIONS_ERROR);
        }

       /*
        * Encode entry requires schema description
        * hence perform encode entry after VmDirSchemaCheckSetAttrDesc
        */
        retVal = VmDirSchemaCheckSetAttrDesc(pSchemaCtx, &e);
        BAIL_ON_VMDIR_ERROR(retVal);

        VmDirReplicationEncodeEntryForRetry(&e, pPageEntry);
    }
    else  // retry case
    {
        // schema context will be cloned as part of the decode entry (VmDirDecodeEntry)
        retVal = VmDirReplicationDecodeEntryForRetry(pSchemaCtx, pPageEntry, &e);
        BAIL_ON_VMDIR_ERROR(retVal);
    }

    retVal = ReplFixUpEntryDn(&e);
    BAIL_ON_VMDIR_ERROR( retVal );

    _VmDirLogReplModifyEntryContent(pPageEntry, &e);

    if (VmDirBervalContentDup( &e.dn, &mr->dn ) != 0)
    {
        VMDIR_LOG_ERROR(VMDIR_LOG_MASK_ALL, "SetupReplModifyRequest: BervalContentDup failed." );
        retVal = LDAP_OPERATIONS_ERROR;
        BAIL_ON_VMDIR_ERROR( retVal );
    }

    // This is strict locking order:
    // Must acquire schema modification mutex before backend write txn begins
    retVal = VmDirSchemaModMutexAcquire(&modOp);
    BAIL_ON_VMDIR_ERROR( retVal );

    modOp.pBEIF = VmDirBackendSelect(mr->dn.lberbv.bv_val);
    assert(modOp.pBEIF);

    if((retVal = VmDirWriteQueuePush(
                    modOp.pBECtx,
                    gVmDirServerOpsGlobals.pWriteQueue,
                    modOp.pWriteQueueEle)) != 0)
    {
        VMDIR_LOG_ERROR(
            VMDIR_LOG_MASK_ALL,
            "%s: failed with error code: %d, error string: %s",
            __FUNCTION__,
            retVal,
            VDIR_SAFE_STRING(modOp.pBEErrorMsg));
        retVal = LDAP_OPERATIONS_ERROR;
        BAIL_ON_VMDIR_ERROR(retVal);
    }

    retVal = VmDirWriteQueueWait(gVmDirServerOpsGlobals.pWriteQueue, modOp.pWriteQueueEle);
    BAIL_ON_VMDIR_ERROR(retVal);

    // ************************************************************************************
    // transaction retry loop begin.  make sure all function within are retry agnostic.
    // ************************************************************************************
txnretry:
    {
    if (bHasTxn)
    {
        modOp.pBEIF->pfnBETxnAbort( modOp.pBECtx );
        bHasTxn = FALSE;
    }

    deadLockRetries++;
    if (deadLockRetries > MAX_DEADLOCK_RETRIES)
    {
        VMDIR_LOG_ERROR(VMDIR_LOG_MASK_ALL, "ReplModifyEntry: Ran out of deadlock retries." );
        retVal = LDAP_LOCK_DEADLOCK;
        BAIL_ON_VMDIR_ERROR( retVal );
    }

    // Transaction needed to process existing/local attribute meta data.
    if ((dbRetVal = modOp.pBEIF->pfnBETxnBegin( modOp.pBECtx, VDIR_BACKEND_TXN_WRITE)) != 0)
    {
        VMDIR_LOG_ERROR(VMDIR_LOG_MASK_ALL, "ReplModifyEntry: pfnBETxnBegin failed with error code: %d, error string: %s",
                  dbRetVal, VDIR_SAFE_STRING(modOp.pBEErrorMsg) );
        retVal = LDAP_OPERATIONS_ERROR;
        BAIL_ON_VMDIR_ERROR( retVal );
    }
    bHasTxn = TRUE;

    if (mr->dn.bvnorm_val == NULL)
    {
        if ((retVal = VmDirNormalizeDN(&mr->dn, pSchemaCtx)) != 0)
        {
            VMDIR_LOG_ERROR(VMDIR_LOG_MASK_ALL, "SetupReplModifyRequest: VmDirNormalizeDN failed on dn %s ",
                    VDIR_SAFE_STRING(mr->dn.lberbv.bv_val));
            retVal = LDAP_OPERATIONS_ERROR;
            BAIL_ON_VMDIR_ERROR( retVal );
        }
    }

    // Get EntryId
    retVal = modOp.pBEIF->pfnBEDNToEntryId(modOp.pBECtx, &mr->dn, &entryId);
    if (retVal != 0)
    {
        switch (retVal)
        {
            case ERROR_BACKEND_ENTRY_NOTFOUND:
                VMDIR_LOG_ERROR(VMDIR_LOG_MASK_ALL, "ReplModifyEntry: entry %s doesn't exist error code %d",
                                VDIR_SAFE_STRING(mr->dn.bvnorm_val), retVal);
                break;
            case LDAP_LOCK_DEADLOCK:
                goto txnretry;
            default:
                VMDIR_LOG_ERROR(VMDIR_LOG_MASK_ALL, "ReplModifyEntry: pfnBEDNToEntryId failed dn %s error code %d",
                                VDIR_SAFE_STRING(mr->dn.bvnorm_val), retVal);
                break;
        }
    }
    BAIL_ON_VMDIR_ERROR( retVal );

    retVal = _VmDirDetatchValueMetaData(&modOp, &e, &pAttrAttrValueMetaData);
    BAIL_ON_VMDIR_ERROR( retVal );

    // need these before DetectAndResolveAttrsConflicts
    modOp.pszPartner = pPageEntry->pszPartner;
    modOp.ulPartnerUSN = pPageEntry->ulPartnerUSN;

    e.eId = entryId;
    if ((retVal = SetupReplModifyRequest(&modOp, &e)) != LDAP_SUCCESS)
    {
        PSZ_METADATA_BUF    pszMetaData = {'\0'};

        //Ignore error - used only for logging
        VmDirMetaDataSerialize(e.attrs[0].pMetaData, pszMetaData);

        switch (retVal)
        {
            case LDAP_NO_SUCH_OBJECT:
                VMDIR_LOG_WARNING(LDAP_DEBUG_REPL,
                          "ReplModifyEntry/SetupReplModifyRequest: %d (Object does not exist). "
                          "DN: %s, first attribute: %s, it's meta data: '%s'. "
                          "Possible replication CONFLICT. Object will get deleted from the system. "
                          "Partner USN %" PRId64,
                          retVal, e.dn.lberbv.bv_val, e.attrs[0].type.lberbv.bv_val,
                          pszMetaData, pPageEntry->ulPartnerUSN);
                break;

            case LDAP_LOCK_DEADLOCK:
                VMDIR_LOG_INFO(VMDIR_LOG_MASK_ALL,
                          "ReplModifyEntry/SetupReplModifyRequest: %d (%s). ",
                          retVal, VDIR_SAFE_STRING( modOp.ldapResult.pszErrMsg ));
                goto txnretry; // Possible retry.

            default:
                VMDIR_LOG_ERROR(VMDIR_LOG_MASK_ALL,
                          "ReplModifyEntry/SetupReplModifyRequest: %d (%s). Partner USN %" PRId64,
                          retVal, VDIR_SAFE_STRING( modOp.ldapResult.pszErrMsg ), pPageEntry->ulPartnerUSN);
                break;
       }
        BAIL_ON_VMDIR_ERROR( retVal );
    }

    // If some mods left after conflict resolution
    if (mr->mods != NULL)
    {
        retVal = _VmDeleteOldValueMetaData(&modOp, mr->mods, entryId);
        BAIL_ON_VMDIR_ERROR( retVal );

        _VmDirLogReplModifyModContent(&modOp.request.modifyReq);

        // SJ-TBD: What happens when DN of the entry has changed in the meanwhile? => conflict resolution.
        // Should objectGuid, instead of DN, be used to uniquely identify an object?
        if ((retVal = VmDirInternalModifyEntry( &modOp )) != LDAP_SUCCESS)
        {
            // If VmDirInternall call failed, reset retVal to LDAP level error space (for B/C)
            retVal = modOp.ldapResult.errCode;

            VMDIR_LOG_ERROR(VMDIR_LOG_MASK_ALL, "ReplModifyEntry: InternalModifyEntry failed. Error: %d, error string %s",
                      retVal, VDIR_SAFE_STRING( modOp.ldapResult.pszErrMsg ));

            switch (retVal)
            {
                case LDAP_LOCK_DEADLOCK:
                    goto txnretry; // Possible retry.

                default:
                    break;
            }
            BAIL_ON_VMDIR_ERROR( retVal );
        }
    }

    VMDIR_LOG_INFO(LDAP_DEBUG_REPL, "ReplModifyEntry: found AttrValueMetaData %s", pAttrAttrValueMetaData?"yes":"no");
    if (pAttrAttrValueMetaData)
    {
        retVal = _VmSetupValueMetaData(pSchemaCtx, &modOp, pAttrAttrValueMetaData, modOp.pWriteQueueEle->usn, entryId);
        BAIL_ON_VMDIR_ERROR( retVal );
        mr = &(modOp.request.modifyReq);
        if (mr->mods != NULL)
        {
            _VmDirLogReplModifyModContent(&modOp.request.modifyReq);

            if ((retVal = VmDirInternalModifyEntry( &modOp )) != LDAP_SUCCESS)
            {
                retVal = modOp.ldapResult.errCode;
                switch (retVal)
                {
                    case LDAP_LOCK_DEADLOCK:
                        goto txnretry;
                    default:
                        VMDIR_LOG_ERROR(VMDIR_LOG_MASK_ALL, "ReplModifyEntry: InternalModifyEntry failed. Error: %d, error string %s",
                                        retVal, VDIR_SAFE_STRING( modOp.ldapResult.pszErrMsg ));
                        break;
                }
                BAIL_ON_VMDIR_ERROR( retVal );
            }
        }
    }

    if ((dbRetVal = modOp.pBEIF->pfnBETxnCommit( modOp.pBECtx)) != 0)
    {
        VMDIR_LOG_ERROR(VMDIR_LOG_MASK_ALL, "ReplModifyEntry: pfnBETxnCommit failed with error code: %d, error string: %s",
                  dbRetVal, VDIR_SAFE_STRING(modOp.pBEErrorMsg) );
        retVal = LDAP_OPERATIONS_ERROR;
        BAIL_ON_VMDIR_ERROR( retVal );
    }
    bHasTxn = FALSE;
    // ************************************************************************************
    // transaction retry loop end.
    // ************************************************************************************
    }

    if (VmDirStringEndsWith(
            BERVAL_NORM_VAL(modOp.request.modifyReq.dn),
            SCHEMA_NAMING_CONTEXT_DN,
            FALSE))
    {
        // schema entry updated, refresh replication schema ctx.
        assert( ppOutSchemaCtx );
        retVal = VmDirSchemaCtxAcquire(&pUpdateSchemaCtx);
        BAIL_ON_VMDIR_ERROR(retVal);
        *ppOutSchemaCtx = pUpdateSchemaCtx;

        VmDirSchemaCtxRelease(pSchemaCtx);
    }

cleanup:
    VmDirWriteQueuePop(gVmDirServerOpsGlobals.pWriteQueue, modOp.pWriteQueueEle);

    // Release schema modification mutex
    VmDirFreeBervalContent(&bvParentDn);
    (VOID)VmDirSchemaModMutexRelease(&modOp);
    VmDirFreeOperationContent(&modOp);
    VmDirFreeEntryContent(&e);
    VmDirFreeAttribute(pAttrAttrValueMetaData);
    return retVal;

error:
    if (bHasTxn)
    {
        modOp.pBEIF->pfnBETxnAbort( modOp.pBECtx );
    }
    VmDirSchemaCtxRelease(pUpdateSchemaCtx);

    goto cleanup;
} // Replicate Modify Entry operation


/*
 * pEntry is off the wire from the supplier and the object it
 * represents may not have the same DN on this consumer.
 * The object GUID will be used to search the local system and
 * determine the local DN that is being used and adjust pEntry
 * to use the same local DN.
 */
static
int
ReplFixUpEntryDn(
    PVDIR_ENTRY pEntry
    )
{
    int                 retVal = LDAP_SUCCESS;
    VDIR_ENTRY_ARRAY    entryArray = {0};
    PVDIR_ATTRIBUTE     currAttr = NULL;
    PVDIR_ATTRIBUTE     prevAttr = NULL;
    PVDIR_ATTRIBUTE     pAttrObjectGUID = NULL;

    // Remove object GUID from list of attributes
    for (prevAttr = NULL, currAttr = pEntry->attrs; currAttr;
         prevAttr = currAttr, currAttr = currAttr->next)
    {
        if (VmDirStringCompareA( currAttr->type.lberbv.bv_val, ATTR_OBJECT_GUID, FALSE ) == 0)
        {
            if (prevAttr == NULL)
            {
                pEntry->attrs = currAttr->next;
            }
            else
            {
                prevAttr->next = currAttr->next;
            }
            pAttrObjectGUID = currAttr;
            break;
        }
    }

    if (!pAttrObjectGUID)
    {
        // Older replication partner does not send object GUID, no fixup needed
        goto cleanup;
    }

    retVal = VmDirSimpleEqualFilterInternalSearch("", LDAP_SCOPE_SUBTREE, ATTR_OBJECT_GUID, pAttrObjectGUID->vals[0].lberbv.bv_val, &entryArray);
    BAIL_ON_VMDIR_ERROR( retVal );

    if (entryArray.iSize != 1)
    {
        // object guid not found - entry missing or object GUID mismatch
        VMDIR_LOG_WARNING( VMDIR_LOG_MASK_ALL,
                "%s got no result from object GUID lookup."
                "Entry (%s) missing or object GUID mismatch",
                __FUNCTION__, pEntry->dn.lberbv_val );
        goto cleanup;
    }

    if (VmDirStringCompareA(entryArray.pEntry[0].dn.lberbv_val, pEntry->dn.lberbv_val, FALSE) == 0)
    {
        // Remote and local object have same DN, no fixup needed
        goto cleanup;
    }

    retVal = VmDirBervalContentDup(&entryArray.pEntry[0].dn, &pEntry->dn);
    BAIL_ON_VMDIR_ERROR(retVal);

cleanup:
    VmDirFreeAttribute( pAttrObjectGUID );
    VmDirFreeEntryArrayContent(&entryArray);
    return retVal;

error:

    goto cleanup;
}

static
int
_VmDirPatchData(
    PVDIR_OPERATION     pOperation
    )
{
    int                 retVal = LDAP_SUCCESS;
    VDIR_ATTRIBUTE *    currAttr = NULL;
    VDIR_ATTRIBUTE *    prevAttr = NULL;
    PVDIR_ENTRY         pEntry = NULL;
    size_t              containerOCLen = VmDirStringLenA( OC_CONTAINER );
    int                 i = 0;

    pEntry = pOperation->request.addReq.pEntry;

    for (prevAttr = NULL, currAttr = pEntry->attrs; currAttr;
         prevAttr = currAttr, currAttr = currAttr->next)
    {
        // map attribute vmwSecurityDescriptor => nTSecurityDescriptor
        if (VmDirStringCompareA( currAttr->type.lberbv.bv_val, ATTR_VMW_OBJECT_SECURITY_DESCRIPTOR, FALSE ) == 0)
        {
            currAttr->type.lberbv.bv_val = ATTR_OBJECT_SECURITY_DESCRIPTOR;
            currAttr->type.lberbv.bv_len = ATTR_OBJECT_SECURITY_DESCRIPTOR_LEN;
            continue;
        }
        // map object class value vmwContainer => container
        if (VmDirStringCompareA( currAttr->type.lberbv.bv_val, ATTR_OBJECT_CLASS, FALSE ) == 0)
        {
            for (i = 0; currAttr->vals[i].lberbv.bv_val != NULL; i++)
            {
                if (VmDirStringCompareA( currAttr->vals[i].lberbv.bv_val, OC_VMW_CONTAINER, FALSE ) == 0)
                {
                    retVal = VmDirAllocateMemory( containerOCLen + 1, (PVOID*)&currAttr->vals[i].lberbv.bv_val );
                    BAIL_ON_VMDIR_ERROR(retVal);

                    retVal = VmDirCopyMemory( currAttr->vals[i].lberbv.bv_val, containerOCLen + 1, OC_CONTAINER,
                                              containerOCLen );
                    BAIL_ON_VMDIR_ERROR(retVal);

                    currAttr->vals[i].lberbv.bv_len = containerOCLen;
                    currAttr->vals[i].bOwnBvVal = TRUE;

                    break;
                }
            }
            continue;
        }
        // remove vmwOrganizationGuid attribute
        if (VmDirStringCompareA( currAttr->type.lberbv.bv_val, ATTR_VMW_ORGANIZATION_GUID, FALSE ) == 0)
        { // Remove "vmwOrganizationGuid" attribute from the list.
            if (prevAttr == NULL)
            {
                pEntry->attrs = currAttr->next;
            }
            else
            {
                prevAttr->next = currAttr->next;
            }
            continue;
        }
    }

cleanup:
    return retVal;

error:
    goto cleanup;
}



/* Create modify request corresponding to the given entry. Main steps are:
 *  - Create replace mods for the "local" attributes.
 *  - Create replace mods for the attributes present in the entry.
 *  - Create delete mods for the attributes that only have attribute meta data, but no attribute.
 *
 *  Also detect that if an object is being deleted, in which case set the correct targetDn.
 */

static
int
SetupReplModifyRequest(
    VDIR_OPERATION *    pOperation,
    PVDIR_ENTRY         pEntry
    )
{
    int                 retVal = LDAP_SUCCESS;
    VDIR_MODIFICATION * mod = NULL;
    VDIR_ATTRIBUTE *    currAttr = NULL;
    unsigned int        i = 0;
    PVDIR_ATTRIBUTE     pAttrAttrMetaData = NULL;
    BOOLEAN             isDeleteObjReq = FALSE;
    VDIR_MODIFICATION * lastKnownDNMod = NULL;
    PVDIR_SCHEMA_CTX    pSchemaCtx = pOperation->pSchemaCtx;
    ModifyReq *         mr = &(pOperation->request.modifyReq);
    VDIR_ENTRY          consumerEntry = {0};
    PLW_HASHMAP         pMetaDataMap = NULL;
    PSTR                pszAttrType = NULL;
    LW_HASHMAP_ITER     iter = LW_HASHMAP_ITER_INIT;
    LW_HASHMAP_PAIR     pair = {NULL, NULL};
    PVMDIR_ATTRIBUTE_METADATA    pSupplierMetaData = NULL;

    VMDIR_LOG_DEBUG(LDAP_DEBUG_REPL, "SetupReplModifyRequest: next entry being replicated/Modified is: %s",
              pEntry->dn.lberbv.bv_val );

    // SJ-TBD: For every replicated Add do we really need to clone the schema context??
    if (pEntry->pSchemaCtx == NULL)
    {
        pEntry->pSchemaCtx = VmDirSchemaCtxClone(pOperation->pSchemaCtx);
    }

    // Make sure Attribute has its ATDesc set
    retVal = VmDirSchemaCheckSetAttrDesc(pEntry->pSchemaCtx, pEntry);
    BAIL_ON_VMDIR_ERROR(retVal);

    retVal = VmDirReplSetAttrNewMetaData(pOperation, pEntry, &pMetaDataMap);
    BAIL_ON_VMDIR_ERROR(retVal);

    for (currAttr = pEntry->attrs; currAttr; currAttr = currAttr->next)
    {
        // Skip attributes that have loser attribute meta data => no mods for them
        if (IS_VMDIR_REPL_ATTR_CONFLICT(currAttr->pMetaData))
        {
            continue;
        }

        if (VmDirStringCompareA(currAttr->type.lberbv.bv_val, ATTR_OBJECT_GUID, FALSE) == 0)
        {
            // Skipping metadata processing for ObjectGUID which should never change.
            continue;
        }

        if (VmDirAllocateMemory(sizeof(VDIR_MODIFICATION), (PVOID *)&mod) != 0)
        {
            VMDIR_LOG_ERROR(VMDIR_LOG_MASK_ALL, "%s: VmDirAllocateMemory error", __FUNCTION__);
            BAIL_WITH_VMDIR_ERROR(retVal, LDAP_OPERATIONS_ERROR);
        }
        mod->operation = MOD_OP_REPLACE;

        retVal = VmDirAttributeInitialize(
                currAttr->type.lberbv.bv_val, currAttr->numVals, pSchemaCtx, &mod->attr);
        BAIL_ON_VMDIR_ERROR(retVal);

        mod->attr.pMetaData = currAttr->pMetaData;
        currAttr->pMetaData = NULL;

        for (i = 0; i < currAttr->numVals; i++)
        {
            if (VmDirBervalContentDup(&currAttr->vals[i], &mod->attr.vals[i]) != 0)
            {
                VMDIR_LOG_ERROR(VMDIR_LOG_MASK_ALL, "%s: BervalContentDup failed.", __FUNCTION__);
                BAIL_WITH_VMDIR_ERROR(retVal, LDAP_OPERATIONS_ERROR);
            }
        }

        if (VmDirStringCompareA(mod->attr.type.lberbv_val, ATTR_IS_DELETED, FALSE) == 0 &&
            VmDirStringCompareA(mod->attr.vals[0].lberbv_val, VMDIR_IS_DELETED_TRUE_STR, FALSE) == 0)
        {
            isDeleteObjReq = TRUE;
        }

        if (VmDirStringCompareA(mod->attr.type.lberbv.bv_val, ATTR_LAST_KNOWN_DN, FALSE) == 0)
        {
            lastKnownDNMod = mod;
        }

        mod->next = mr->mods;
        mr->mods = mod;
        mr->numMods++;
    }

    // Create Delete Mods
    while (LwRtlHashMapIterate(pMetaDataMap, &iter, &pair))
    {
        pszAttrType = (PSTR) pair.pKey;
        pSupplierMetaData = (PVMDIR_ATTRIBUTE_METADATA) pair.pValue;

        pair.pKey = pair.pValue = NULL;
        retVal = LwRtlHashMapRemove(pMetaDataMap, pszAttrType, &pair);
        BAIL_ON_VMDIR_ERROR(retVal);

        // Skip metadata processing for ObjectGUID which should never change.
        if (VmDirStringCompareA(pszAttrType, ATTR_OBJECT_GUID, FALSE) == 0)
        {
            continue;
        }

        if (VmDirAllocateMemory(sizeof(VDIR_MODIFICATION), (PVOID *)&mod) != 0)
        {
            VMDIR_LOG_ERROR(VMDIR_LOG_MASK_ALL, "%s: VmDirAllocateMemory error", __FUNCTION__);
            BAIL_WITH_VMDIR_ERROR(retVal, LDAP_OPERATIONS_ERROR);
        }
        mod->operation = MOD_OP_DELETE;

        retVal = VmDirAttributeInitialize(pszAttrType, 0, pSchemaCtx, &mod->attr);
        BAIL_ON_VMDIR_ERROR(retVal);

        // Set localUsn in the metaData
        retVal = VmDirMetaDataSetLocalUsn(pSupplierMetaData, pOperation->pWriteQueueEle->usn);
        BAIL_ON_VMDIR_ERROR(retVal);

        mod->attr.pMetaData = pSupplierMetaData;
        pair.pValue = NULL;

        mod->next = mr->mods;
        mr->mods = mod;
        mr->numMods++;

        VmDirFreeMetaDataMapPair(&pair, NULL);
    }

    if (isDeleteObjReq)
    {
        if (VmDirBervalContentDup( &lastKnownDNMod->attr.vals[0], &mr->dn ) != 0)
        {
            VMDIR_LOG_ERROR(VMDIR_LOG_MASK_ALL, "ReplDeleteEntry: BervalContentDup failed." );
            BAIL_WITH_VMDIR_ERROR(retVal, LDAP_OPERATIONS_ERROR);
        }
    }

cleanup:
    if (pMetaDataMap)
    {
        LwRtlHashMapClear(
                pMetaDataMap,
                VmDirFreeMetaDataMapPair,
                NULL);
        LwRtlFreeHashMap(&pMetaDataMap);
    }
    // pAttrAttrMetaData is local, needs to be freed within the call
    VmDirFreeAttribute(pAttrAttrMetaData);
    VmDirFreeEntryContent(&consumerEntry);

    return retVal;

error:
    VMDIR_LOG_ERROR(VMDIR_LOG_MASK_ALL, "failed, error (%d)", retVal);
    goto cleanup;
}

/*
 * Determine whether the supplier's attr-value-meta-data wins by checking it against local
 * attr-meta-data and local attr-value-meta-data.
 * It first compares the <version><invocation-id> of that in local attr-meta-data which was
 * applied either in the previous transaction or the previous modification in the current transactions.
 * Then if the <version><invocation-id> matches, it looks up the local server to see if the same
 * attr-value-meta-data existi: if supplier's attr-value-meta-data has a newer timestamp then
 * it wins and inScope set to TRUE.
 */
static
int
_VmDirAttrValueMetaResolve(
    PVDIR_OPERATION pModOp,
    PVDIR_ATTRIBUTE pAttr,
    PVDIR_BERVALUE  suppAttrMetaValue,
    ENTRYID         entryId,
    PBOOLEAN        pInScope
    )
{
    int                 retVal = 0;
    char               *pc = NULL, *ppc = NULL;
    char               *ps = NULL, *pps = NULL, *ps_ts = NULL;
    int                 rc = 0;
    int                 psv_len = 0;
    VDIR_BERVALUE      *pAVmeta = NULL;
    DEQUE               valueMetaData = {0};
    PSZ_METADATA_BUF    pszSupplierMetaData = {'\0'};

    *pInScope = TRUE;
    retVal = pModOp->pBEIF->pfnBEGetAttrMetaData(pModOp->pBECtx, pAttr, entryId);
    BAIL_ON_VMDIR_ERROR(retVal);

    ps = suppAttrMetaValue->lberbv.bv_val;
    VALUE_META_TO_NEXT_FIELD(ps, 2);
    pps = VmDirStringChrA(VmDirStringChrA(ps, ':')+1, ':');
    *pps = '\0';
    //ps points to supplier attr-value-meta "<version><originating-server-id>"

    retVal = VmDirStringPrintFA(
            pszSupplierMetaData,
            VMDIR_MAX_ATTR_META_DATA_LEN,
            "%"PRId64":%s",
            pAttr->pMetaData->version,
            pAttr->pMetaData->pszOrigInvoId);
    BAIL_ON_VMDIR_ERROR(retVal);

    rc = strcmp(ps, pszSupplierMetaData);
    *pps = ':';
    if (rc)
    {
        //consumer <version><originating-server-id> in metaValueData
        //   not match supplier's <version<<originating-server-id> in metaData
        //   this value-meta-data out of scope
        *pInScope = FALSE;
        goto cleanup;
    }

    retVal = pModOp->pBEIF->pfnBEGetAttrValueMetaData(pModOp->pBECtx, entryId, pAttr->pATDesc->usAttrID, &valueMetaData);
    BAIL_ON_VMDIR_ERROR(retVal);

    ps = suppAttrMetaValue->lberbv.bv_val;
    VALUE_META_TO_NEXT_FIELD(ps, 8);
    pps = VmDirStringChrA(ps, ':');
    //ps points to <value-size>:<value>
    *pps = '\0';
    psv_len = VmDirStringToIA(ps);
    *pps = ':';
    VALUE_META_TO_NEXT_FIELD(ps, 1);
    //ps now points to <value> of supplier's attr-value-meta-data
    ps_ts = suppAttrMetaValue->lberbv.bv_val;
    VALUE_META_TO_NEXT_FIELD(ps_ts, 5);
    //ps_ts points to <value-change-originating time>

    while(!dequeIsEmpty(&valueMetaData))
    {
        int pcv_len = 0;
        char *pc_ts = NULL;

        VmDirFreeBerval(pAVmeta);
        pAVmeta = NULL;

        dequePopLeft(&valueMetaData, (PVOID*)&pAVmeta);
        pc = pAVmeta->lberbv.bv_val;
        VALUE_META_TO_NEXT_FIELD(pc, 8);
        ppc = VmDirStringChrA(pc, ':');
        *ppc = '\0';
        pcv_len = VmDirStringToIA(pc);
        *ppc = ':';
        if (psv_len != pcv_len)
        {
            continue;
        }
        VALUE_META_TO_NEXT_FIELD(pc, 1);
        if (memcmp(ps, pc, pcv_len))
        {
            continue;
        }

        // Now found attr-value-meta-data with the same attribute value.
        // If this one' timestamp is later than the supplier's,
        // then the consumer won, this may occur like: add attr-a/value-a, then delete attr-a/value-a (in two repl cycles)
        pc_ts = pAVmeta->lberbv.bv_val;
        VALUE_META_TO_NEXT_FIELD(pc_ts, 5);
        rc = strncmp(pc_ts, ps_ts, VMDIR_ORIG_TIME_STR_LEN);
        if (rc > 0)
        {
          // If any of newer attr-value-meta-data with that entryid/attr-id/attr-value in the consumer,
          //   then the supplier attr-value-meta-data lose
          *pInScope = FALSE;
          VMDIR_LOG_DEBUG(LDAP_DEBUG_REPL, "_VmDirAttrValueMetaResolve: supplier attr-value-meta lose: %s consumer: %s",
                VDIR_SAFE_STRING(suppAttrMetaValue->lberbv.bv_val), VDIR_SAFE_STRING(pAVmeta->lberbv.bv_val));
        }
    }
    if (*pInScope)
    {
        VMDIR_LOG_DEBUG(LDAP_DEBUG_REPL, "_VmDirAttrValueMetaResolve: supplier attr-value-meta won: %s",
                VDIR_SAFE_STRING(suppAttrMetaValue->lberbv.bv_val));
    }

cleanup:
    VmDirFreeBerval(pAVmeta);
    VmDirFreeAttrValueMetaDataContent(&valueMetaData);
    return retVal;

error:
    goto cleanup;
}

/*
 * First determine if each attribute value meta data in pAttrAttrValueMetaData
 * win those in local database (if they exist locally); then create mod for
 * adding/deleting the attribute value for those with winning attribute value meta data
 * from supplier.
 */
static
int
_VmSetupValueMetaData(
    PVDIR_SCHEMA_CTX    pSchemaCtx,
    PVDIR_OPERATION     pModOp,
    PVDIR_ATTRIBUTE     pAttrAttrValueMetaData,
    USN                 localUsn,
    ENTRYID             entryId
    )
{
    int                   i = 0;
    int                   retVal = 0;
    VDIR_BERVALUE         *pAVmeta = NULL;
    PSZ_METADATA_BUF      av_meta_pre = {'\0'};
    int                   new_av_len = 0;
    BOOLEAN               inScope = FALSE;
    VDIR_MODIFICATION     *mod = NULL, *modp = NULL, *pre_modp = NULL;
    ModifyReq             *mr = NULL;
    VDIR_MODIFICATION     *currMod = NULL;
    VDIR_MODIFICATION     *tmpMod = NULL;
    PVDIR_ATTRIBUTE       pAttr = NULL;
    char                  *p = NULL, *pp = NULL;

    mr = &(pModOp->request.modifyReq);
    //clear all mods that should have been applied.
    for ( currMod = mr->mods; currMod != NULL; )
    {
        tmpMod = currMod->next;
        VmDirModificationFree(currMod);
        currMod = tmpMod;
    }
    mr->mods = NULL;
    mr->numMods = 0;

    //format of a value meta data item:
    //       <attr-name>:<local-usn>:<version-no>:<originating-server-id>:<value-change-originating-server-id>
    //       :<value-change-originating time>:<value-change-originating-usn>:
    //Remaining portion of attr-value-meta-data:   <opcode>:<value-size>:<value>
    for ( i=0; i<(int)pAttrAttrValueMetaData->numVals; i++ )
    {
       if (!VmDirValidValueMetaEntry(&pAttrAttrValueMetaData->vals[i]))
       {
          retVal = ERROR_INVALID_PARAMETER;
          VMDIR_LOG_INFO(VMDIR_LOG_MASK_ALL, "_VmSetupValueMetaData: invalid attr-value-meta: %s",
                         VDIR_SAFE_STRING(pAttrAttrValueMetaData->vals[i].lberbv.bv_val));
          BAIL_ON_VMDIR_ERROR(retVal);
       }

       p = pAttrAttrValueMetaData->vals[i].lberbv.bv_val;
       pp = VmDirStringChrA(p, ':');
       *pp = '\0';
       // p points to attr-name
       VmDirFreeAttribute(pAttr);
       pAttr = NULL;

       retVal = VmDirAttributeAllocate(p, 1, pSchemaCtx, &pAttr);
       BAIL_ON_VMDIR_ERROR(retVal);

       *pp = ':';
       retVal = _VmDirAttrValueMetaResolve(pModOp, pAttr, &pAttrAttrValueMetaData->vals[i], entryId, &inScope);
       BAIL_ON_VMDIR_ERROR(retVal);

       if (!inScope)
       {
          continue;
       }

       VALUE_META_TO_NEXT_FIELD(p, 2);
       // p now points to <version>...
       // Need to replace supp's <local-usn> with new locally generated local-usn.
       retVal = VmDirStringNPrintFA(av_meta_pre, sizeof(av_meta_pre), sizeof(av_meta_pre) - 1,
                    "%s:%" PRId64 ":", pAttr->type.lberbv.bv_val, localUsn);
       BAIL_ON_VMDIR_ERROR(retVal);

       //av_meta_pre contains "<attr-name>:<new-local-usn>:"
       //re-calculate the length of attr-value-meta-data.
       new_av_len =  (int)strlen(av_meta_pre) +
                     (int)pAttrAttrValueMetaData->vals[i].lberbv.bv_len -
                     (int)(p - pAttrAttrValueMetaData->vals[i].lberbv.bv_val);
       retVal = VmDirAllocateMemory(sizeof(VDIR_BERVALUE), (PVOID)&pAVmeta);
       BAIL_ON_VMDIR_ERROR(retVal);

       retVal = VmDirAllocateMemory(new_av_len, (PVOID)&pAVmeta->lberbv.bv_val);
       BAIL_ON_VMDIR_ERROR(retVal);

       pAVmeta->bOwnBvVal = TRUE;
       pAVmeta->lberbv.bv_len = new_av_len;
       retVal = VmDirCopyMemory(pAVmeta->lberbv.bv_val, new_av_len, av_meta_pre, strlen(av_meta_pre));
       BAIL_ON_VMDIR_ERROR(retVal);

       retVal = VmDirCopyMemory(pAVmeta->lberbv.bv_val+strlen(av_meta_pre),
                                 new_av_len - strlen(av_meta_pre), p, new_av_len - strlen(av_meta_pre));
       BAIL_ON_VMDIR_ERROR(retVal);

       //Write the attr-value-meta-data to backend index database.
       retVal = dequePush(&pAttr->valueMetaDataToAdd, (PVOID)pAVmeta);
       BAIL_ON_VMDIR_ERROR(retVal);
       pAVmeta = NULL;

       retVal = pModOp->pBEIF->pfnBEUpdateAttrValueMetaData( pModOp->pBECtx, entryId, pAttr->pATDesc->usAttrID,
                                                             BE_INDEX_OP_TYPE_UPDATE, &pAttr->valueMetaDataToAdd );
       BAIL_ON_VMDIR_ERROR(retVal);

       //Now create mod for attribute value add/delete.
       retVal = VmDirAllocateMemory( sizeof(VDIR_MODIFICATION), (PVOID *)&mod);
       BAIL_ON_VMDIR_ERROR(retVal);

       VALUE_META_TO_NEXT_FIELD(p, 5);
       // p points to <opcode>...
       pp = VmDirStringChrA(p, ':');
       *pp = '\0';
       mod->operation = VmDirStringToIA(p);
       *pp = ':';
       retVal = VmDirAttributeInitialize(pAttr->type.lberbv.bv_val, 1, pSchemaCtx, &mod->attr );
       BAIL_ON_VMDIR_ERROR( retVal );

       VALUE_META_TO_NEXT_FIELD(p, 1);
       // p points to <value-size><value>
       pp = VmDirStringChrA(p, ':');
       *pp = '\0';
       mod->attr.vals[0].lberbv.bv_len = VmDirStringToIA(p);
       *pp = ':';
       VALUE_META_TO_NEXT_FIELD(p, 1);
       //p points to <value>
       retVal = VmDirAllocateMemory(mod->attr.vals[0].lberbv.bv_len + 1, (PVOID *)&mod->attr.vals[0].lberbv.bv_val);
       BAIL_ON_VMDIR_ERROR( retVal );

       mod->attr.vals[0].bOwnBvVal = TRUE;
       retVal = VmDirCopyMemory(mod->attr.vals[0].lberbv.bv_val, mod->attr.vals[0].lberbv.bv_len,
                                 p, mod->attr.vals[0].lberbv.bv_len);
       BAIL_ON_VMDIR_ERROR( retVal );

       for( modp=mr->mods; modp; pre_modp=modp,modp=modp->next )
       {
           if (modp->attr.pATDesc->usAttrID == mod->attr.pATDesc->usAttrID &&
               modp->operation == mod->operation)
           {
               break;
           }
       }

       if (modp == NULL)
       {
           if (pre_modp == NULL)
           {
               mr->mods = mod;
           }
           else
           {
               pre_modp->next = mod;
           }
           mr->numMods++;
           mod = NULL;
       }
       else
       {
           // add/delete attr value on the same attribute exists, merge the new mod into it.
           retVal = VmDirReallocateMemoryWithInit( modp->attr.vals, (PVOID*)(&(modp->attr.vals)),
                         (modp->attr.numVals + 2)*sizeof(VDIR_BERVALUE), (modp->attr.numVals + 1)*sizeof(VDIR_BERVALUE));
           BAIL_ON_VMDIR_ERROR(retVal);
           retVal = VmDirBervalContentDup(&mod->attr.vals[0], &modp->attr.vals[modp->attr.numVals]);
           BAIL_ON_VMDIR_ERROR(retVal);
           modp->attr.numVals++;
           memset(&(modp->attr.vals[modp->attr.numVals]), 0, sizeof(VDIR_BERVALUE) );
           VmDirModificationFree(mod);
           mod = NULL;
       }
    }

cleanup:
    VmDirFreeAttribute(pAttr);
    return retVal;

error:
    VmDirFreeBerval(pAVmeta);
    VmDirModificationFree(mod);
    goto cleanup;
}

/*
 * Detach attribute value meta data from the entry's attributes,
 * and set ppAttrAttrValueMetaData to the attribute value meta attribute
 * so that it will be handled seperated.
 */
static
int
_VmDirDetatchValueMetaData(
    PVDIR_OPERATION     pOperation,
    PVDIR_ENTRY         pEntry,
    PVDIR_ATTRIBUTE *   ppAttrAttrValueMetaData
    )
{
    int                 retVal = LDAP_SUCCESS;
    VDIR_ATTRIBUTE *    currAttr = NULL;
    VDIR_ATTRIBUTE *    prevAttr = NULL;
    PVDIR_ATTRIBUTE     pAttrAttrValueMetaData = NULL;

    *ppAttrAttrValueMetaData = NULL;
    for ( prevAttr = NULL, currAttr = pEntry->attrs;
          currAttr;
          prevAttr = currAttr, currAttr = currAttr->next )
    {
        if (VmDirStringCompareA( currAttr->type.lberbv.bv_val, ATTR_ATTR_VALUE_META_DATA, FALSE ) == 0)
        { // Remove "attrValueMetaData" attribute from the list
            if (prevAttr == NULL)
            {
                pEntry->attrs = currAttr->next;
            }
            else
            {
                prevAttr->next = currAttr->next;
            }
            *ppAttrAttrValueMetaData = pAttrAttrValueMetaData = currAttr;
            goto cleanup;
        }
    }

cleanup:
    return retVal;
}

/* If any mod is a MOD_OP_REPLACE on a multi-value attribute,
 * delete that attribute's attr-value-meta-data
 */
static
int
_VmDeleteOldValueMetaData(
    PVDIR_OPERATION     pModOp,
    PVDIR_MODIFICATION  pMods,
    ENTRYID             entryId
    )
{
    int retVal = LDAP_SUCCESS;
    VDIR_MODIFICATION * modp = NULL;
    DEQUE valueMetaDataToDelete = {0};

    for( modp=pMods; modp; modp=modp->next )
    {
         if (modp->operation != MOD_OP_REPLACE || modp->attr.pATDesc->bSingleValue)
         {
             continue;
         }
         retVal = pModOp->pBEIF->pfnBEGetAttrValueMetaData(pModOp->pBECtx, entryId, modp->attr.pATDesc->usAttrID, &valueMetaDataToDelete);
         BAIL_ON_VMDIR_ERROR(retVal);
         if (dequeIsEmpty(&valueMetaDataToDelete))
         {
             continue;
         }
         retVal = pModOp->pBEIF->pfnBEUpdateAttrValueMetaData( pModOp->pBECtx, entryId, modp->attr.pATDesc->usAttrID,
                                                             BE_INDEX_OP_TYPE_DELETE, &valueMetaDataToDelete );
         BAIL_ON_VMDIR_ERROR(retVal);
    }

cleanup:
    VmDirFreeAttrValueMetaDataContent(&valueMetaDataToDelete);
    return retVal;

error:
    goto cleanup;
}

/*
 * Attach and alter attribute value meta data to that attribute in pEntry
 * so that they can be inserted into the backend index when the
 * entry is added to backend.
 */
static
int
_VmDirAttachValueMetaData(
    PVDIR_ATTRIBUTE pAttrAttrValueMetaData,
    PVDIR_ENTRY     pEntry,
    USN             localUsn
    )
{
    int i = 0;
    int retVal = 0;
    char *p = NULL;
    VDIR_BERVALUE *pAVmeta = NULL;

    if (pAttrAttrValueMetaData == NULL)
    {
        goto cleanup;
    }

    for (i = 0; pAttrAttrValueMetaData->vals[i].lberbv.bv_val != NULL; i++)
    {
        if (!VmDirValidValueMetaEntry(&pAttrAttrValueMetaData->vals[i]))
        {
            retVal = ERROR_INVALID_PARAMETER;
            VMDIR_LOG_INFO(VMDIR_LOG_MASK_ALL, "_VmDirAttachValueMetaData: invalid attr-value-meta: %s",
                     VDIR_SAFE_STRING(pAttrAttrValueMetaData->vals[i].lberbv.bv_val));
            BAIL_ON_VMDIR_ERROR(retVal);
        }
        if (pAttrAttrValueMetaData->vals[i].lberbv.bv_len != 0)
        {
            PVDIR_ATTRIBUTE attr = NULL;
            p = VmDirStringChrA( pAttrAttrValueMetaData->vals[i].lberbv.bv_val, ':');
            *p = '\0';
            attr = VmDirEntryFindAttribute(pAttrAttrValueMetaData->vals[i].lberbv.bv_val, pEntry);
            *p = ':';
            if (attr)
            {
               int                 new_av_len = 0;
               PSZ_METADATA_BUF    av_meta_pre = {'\0'};

               VALUE_META_TO_NEXT_FIELD(p, 2);
               // p now points to <version>...
               retVal = VmDirStringNPrintFA(av_meta_pre, sizeof(av_meta_pre), sizeof(av_meta_pre) -1,
                            "%s:%" PRId64 ":", attr->type.lberbv.bv_val, localUsn);
               BAIL_ON_VMDIR_ERROR(retVal);

               //av_meta_pre contains "<attr-name>:<new-local-usn>:"
               //re-calculate the length of attr-value-meta-data.
               new_av_len =  (int)strlen(av_meta_pre) +
                             (int)pAttrAttrValueMetaData->vals[i].lberbv.bv_len -
                             (int)(p - pAttrAttrValueMetaData->vals[i].lberbv.bv_val);
               retVal = VmDirAllocateMemory(sizeof(VDIR_BERVALUE), (PVOID)&pAVmeta);
               BAIL_ON_VMDIR_ERROR(retVal);

               retVal = VmDirAllocateMemory(new_av_len, (PVOID)&pAVmeta->lberbv.bv_val);
               BAIL_ON_VMDIR_ERROR(retVal);

               pAVmeta->bOwnBvVal = TRUE;
               pAVmeta->lberbv.bv_len = new_av_len;
               retVal = VmDirCopyMemory(pAVmeta->lberbv.bv_val, new_av_len, av_meta_pre, strlen(av_meta_pre));
               BAIL_ON_VMDIR_ERROR(retVal);

               retVal = VmDirCopyMemory(pAVmeta->lberbv.bv_val+strlen(av_meta_pre), new_av_len - strlen(av_meta_pre),
                                         p, new_av_len - strlen(av_meta_pre));
               BAIL_ON_VMDIR_ERROR(retVal);

               retVal = dequePush(&attr->valueMetaDataToAdd, pAVmeta);
               BAIL_ON_VMDIR_ERROR(retVal);
               pAVmeta = NULL;
            }
        }
    }

cleanup:
    return retVal;

error:
    VmDirFreeBerval(pAVmeta);
    goto cleanup;
}


static
VOID
_VmDirLogReplEntryContent(
    PVDIR_ENTRY                   pEntry
    )
{
    PVDIR_ATTRIBUTE pAttr = NULL;
    int             iCnt = 0;

    for (pAttr = pEntry->attrs; pAttr; pAttr = pAttr->next)
    {
        for (iCnt=0; iCnt < pAttr->numVals; iCnt++)
        {
            PCSTR pszLogValue = (0 == VmDirStringCompareA( pAttr->type.lberbv.bv_val, ATTR_USER_PASSWORD, FALSE)) ?
                                  "XXX" : pAttr->vals[iCnt].lberbv_val;

            if (iCnt < MAX_NUM_CONTENT_LOG)
            {
                VMDIR_LOG_INFO( LDAP_DEBUG_REPL_ATTR, "%s %s %d (%.*s)",
                    __FUNCTION__,
                    pAttr->type.lberbv.bv_val,
                    iCnt+1,
                    VMDIR_MIN(pAttr->vals[iCnt].lberbv_len, VMDIR_MAX_LOG_OUTPUT_LEN),
                    VDIR_SAFE_STRING(pszLogValue));
            }
            else if (iCnt == MAX_NUM_CONTENT_LOG)
            {
                VMDIR_LOG_INFO( LDAP_DEBUG_REPL_ATTR, "%s Total value count %d)", __FUNCTION__, pAttr->numVals);
            }
            else
            {
                break;
            }
        }
    }
}

static
VOID
_VmDirLogReplAddEntryContent(
    PVMDIR_REPLICATION_PAGE_ENTRY pPageEntry,
    PVDIR_ENTRY                   pEntry
    )
{
    VMDIR_LOG_INFO( LDAP_DEBUG_REPL_ATTR,
              "%s, DN:%s, SYNC_STATE:ADD, partner USN:%" PRId64,
              __FUNCTION__,
              pPageEntry->pszDn,
              pPageEntry->ulPartnerUSN);

    if (VmDirLogLevelAndMaskTest(VMDIR_LOG_VERBOSE, LDAP_DEBUG_REPL_ATTR))
    {
        _VmDirLogReplEntryContent(pEntry);
    }
}

static
VOID
_VmDirLogReplDeleteEntryContent(
    PVMDIR_REPLICATION_PAGE_ENTRY pPageEntry,
    PVDIR_ENTRY                   pEntry
    )
{
    VMDIR_LOG_INFO( LDAP_DEBUG_REPL_ATTR,
              "%s, DN:%s SYNC_STATE:Delete, partner USN:%" PRId64 " local DN: %s",
              __FUNCTION__,
              pPageEntry->pszDn,
              pPageEntry->ulPartnerUSN,
              pEntry->dn.lberbv_val);
}

static
VOID
_VmDirLogReplModifyEntryContent(
    PVMDIR_REPLICATION_PAGE_ENTRY pPageEntry,
    PVDIR_ENTRY                   pEntry
    )
{
    VMDIR_LOG_INFO( LDAP_DEBUG_REPL_ATTR,
              "%s, DN:%s, SYNC_STATE:Modify, partner USN:%" PRId64,
              __FUNCTION__,
              pPageEntry->pszDn,
              pPageEntry->ulPartnerUSN);

    _VmDirLogReplEntryContent(pEntry);
}

static
VOID
_VmDirLogReplModifyModContent(
    ModifyReq*  pModReq
    )
{
    PVDIR_MODIFICATION  pMod = pModReq->mods;
    int                 iCnt = 0;

    for (; pMod; pMod = pMod->next)
    {
        for (iCnt=0; iCnt < pMod->attr.numVals; iCnt++)
        {
            PCSTR pszLogValue = (VmDirIsSensitiveAttr(pMod->attr.type.lberbv.bv_val)) ?
                                  "XXX" : pMod->attr.vals[iCnt].lberbv_val;

            if (iCnt < MAX_NUM_CONTENT_LOG)
            {
                VMDIR_LOG_INFO( VMDIR_LOG_MASK_ALL,
                    "%s MOD %d, %s, %s: (%.*s)",
                    __FUNCTION__,
                    iCnt+1,
                    VmDirLdapModOpTypeToName(pMod->operation),
                    VDIR_SAFE_STRING(pMod->attr.type.lberbv.bv_val),
                    VMDIR_MIN(pMod->attr.vals[iCnt].lberbv_len, VMDIR_MAX_LOG_OUTPUT_LEN),
                    VDIR_SAFE_STRING(pszLogValue));
            }
            else if (iCnt == MAX_NUM_CONTENT_LOG)
            {
                VMDIR_LOG_INFO( VMDIR_LOG_MASK_ALL,
                    "%s Total value count %d)", __FUNCTION__, pMod->attr.numVals);
            }
            else
            {
                break;
            }
        }
    }
}
