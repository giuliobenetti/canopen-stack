/******************************************************************************
   Copyright 2020 Embedded Office GmbH & Co. KG

   Licensed under the Apache License, Version 2.0 (the "License");
   you may not use this file except in compliance with the License.
   You may obtain a copy of the License at

       http://www.apache.org/licenses/LICENSE-2.0

   Unless required by applicable law or agreed to in writing, software
   distributed under the License is distributed on an "AS IS" BASIS,
   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
   See the License for the specific language governing permissions and
   limitations under the License.
******************************************************************************/

/******************************************************************************
* INCLUDES
******************************************************************************/

#include "co_core.h"

/******************************************************************************
* PUBLIC FUNCTIONS
******************************************************************************/

CO_CSDO * COCSdoFind(struct CO_NODE_T *node, uint8_t num) {
    return NULL;
}

CO_ERR COCSdoRequestUpload(CO_CSDO *csdo,
                           uint16_t index,
                           uint8_t sub,
                           CO_CSDO_UP_CALLBACK_T callback,
                           uint32_t timeout) {
    return CO_ERR_NONE;
}

CO_ERR COCSdoRequestDownload(CO_CSDO *csdo,
                             uint16_t index,
                             uint8_t sub,
                             void *value,
                             uint32_t size,
                             CO_CSDO_DN_CALLBACK_T callback,
                             uint32_t timeout) {
    return CO_ERR_NONE;
}

/******************************************************************************
* PRIVATE FUNCTIONS
******************************************************************************/

void COCSdoInit(CO_CSDO *csdo, struct CO_NODE_T *node) {
    uint8_t n;

    for (n = 0; n < CO_CSDO_N; n++) {
        COCSdoReset(csdo, n, node);
        COCSdoEnable(csdo, n);
    }
}

void COCSdoReset(CO_CSDO *csdo, uint8_t num, struct CO_NODE_T *node) {
    CO_CSDO    *csdonum;
    uint32_t    offset;
    int16_t     tid;

    if (csdo == 0) {
        return;
    }
    if (num > CO_CSDO_N) {
        return;
    }

    /* TODO: Transmit abort request if SDO client is reset during transfer */

    /* Reset handle */
    csdonum                     = &csdo[num];
    csdonum->Node               = node;
    csdonum->Frm                = 0;
    csdonum->RxId               = CO_SDO_ID_OFF;
    csdonum->TxId               = CO_SDO_ID_OFF;
    csdonum->State              = CO_CSDO_STATE_INVALID;

    /* Reset transfer buffer instance */
    offset                      = num * CO_SDO_BUF_BYTE;
    csdonum->Buf.Start          = &node->CSdoBuf[offset];
    csdonum->Buf.Cur            = &node->CSdoBuf[offset];
    csdonum->Buf.Num            = 0;

    /* Reset transfer context */
    csdonum->Tfer.Csdo          = csdonum;
    csdonum->Tfer.Type          = CO_CSDO_TRANSFER_NONE;
    csdonum->Tfer.Abort         = 0;
    csdonum->Tfer.Idx           = 0;
    csdonum->Tfer.Sub           = 0;

    csdonum->Tfer.Timeout       = 0;
    csdonum->Tfer.OnDownload    = 0;
    csdonum->Tfer.OnUpload      = 0;

    if (csdonum->Tfer.Tmr >= 0) {
        tid = COTmrDelete(&node->Tmr, csdonum->Tfer.Tmr);
        if (tid < 0) {
            node->Error = CO_ERR_TMR_DELETE;
            return;
        }
    }
    csdonum->Tfer.Tmr           = -1;
}

void COCSdoEnable(CO_CSDO *csdo, uint8_t num) {
    uint32_t    rxId, txId;
    CO_NODE    *node;
    CO_CSDO    *csdonum;
    CO_ERR      err;

    if (num >= CO_CSDO_N) {
        return;
    }
    csdonum                     = &csdo[num];
    csdonum->RxId               = CO_SDO_ID_OFF;
    csdonum->TxId               = CO_SDO_ID_OFF;
    csdonum->State              = CO_CSDO_STATE_INVALID;

    node = csdo->Node;
    err = CODictRdLong(&node->Dict, CO_DEV(0x1280 + num, 1), &txId);
    if (err != CO_ERR_NONE) {
        return;
    }
    err = CODictRdLong(&node->Dict, CO_DEV(0x1280 + num, 2), &rxId);
    if (err != CO_ERR_NONE) {
        return;
    }

    if (((rxId & CO_SDO_ID_OFF) == 0) &&
        ((txId & CO_SDO_ID_OFF) == 0)){
        csdonum->TxId = txId;
        csdonum->RxId = rxId;
    }
}

CO_CSDO *COCSdoCheck(CO_CSDO *csdo, CO_IF_FRM *frm) {
    CO_CSDO    *result;
    uint8_t     n;

    if (frm != 0) {
        n = 0;
        while ((n < CO_CSDO_N) && (result == 0)) {
            if (CO_GET_ID(frm) == csdo[n].RxId) {
                CO_SET_ID(frm, csdo[n].TxId);
                csdo[n].Frm   = frm;
                csdo[n].Abort = 0;
                result = &csdo[n];
            }
            n++;
        }
    }

    return (result);
}

CO_ERR COCSdoResponse(CO_CSDO *csdo) {
    return CO_ERR_SDO_SILENT;
}

CO_ERR COCSdoUploadExpedited(CO_CSDO *csdo) {
    return CO_ERR_NONE;
}

CO_ERR COCSdoDownloadExpedited(CO_CSDO *csdo) {
    return CO_ERR_NONE;
}

void COCSdoTimeout(void *parg) {
    CO_CSDO    *csdo;
    int16_t     tid;

    csdo = (CO_CSDO *) parg;
    if (csdo->State == CO_CSDO_STATE_BUSY) {
        /* Abort SDO transfer because of timeout */
        COCSdoAbort(csdo, CO_SDO_ERR_TIMEOUT);
        /* Finalize aborted transfer */
        COCSdoTransferFinalize(csdo);
    }
}

void COCSdoAbort(CO_CSDO *csdo, uint32_t err) {
    CO_IF_FRM frm;

    /*
     * In case of timeout, frame is not assigned
     * to SDO client handle. A local instance of
     * frame will be attached and transmitted
     * directly during function call.
     */
    if (err == CO_SDO_ERR_TIMEOUT) {
        csdo->Frm = &frm;
        CO_SET_ID(csdo->Frm, csdo->TxId);
    }

    csdo->Tfer.Abort = err;

    CO_SET_BYTE(csdo->Frm,           0x80, 0);
    CO_SET_WORD(csdo->Frm, csdo->Tfer.Idx, 1);
    CO_SET_BYTE(csdo->Frm, csdo->Tfer.Sub, 3);
    CO_SET_LONG(csdo->Frm,            err, 4);

    if (err == CO_SDO_ERR_TIMEOUT) {
        COIfCanSend(&csdo->Node->If, csdo->Frm);
    }
}

void COCSdoTransferFinalize(CO_CSDO *csdo) {
    uint16_t    idx;
    uint8_t     sub;
    uint32_t    code;
    void       *data;
    uint32_t    size;
    int16_t     tid;

    if (csdo->State = CO_CSDO_STATE_BUSY) {
        /* Fetch transfer information */
        idx         = csdo->Tfer.Idx;
        sub         = csdo->Tfer.Sub;
        code        = csdo->Tfer.Abort;
        if (csdo->Tfer.Type == CO_CSDO_TRANSFER_UPLOAD) {
            /* Fetch additional information regarding stored data */
            data    = csdo->Buf.Cur;
            size    = csdo->Buf.Num;
            /* Execute user callback */
            if (csdo->Tfer.OnUpload != 0) {
                csdo->Tfer.OnUpload(csdo, idx, sub, code, data, size);
            }
        } else if (csdo->Tfer.Type == CO_CSDO_TRANSFER_DOWNLOAD) {
            /* Execute user callback */
            if (csdo->Tfer.OnDownload != 0) {
                csdo->Tfer.OnDownload(csdo,idx, sub, code);
            }
        } else {
            /* TODO: This situation should not happen, ensure error reporting */
        }

        /* Reset finished transfer information */
        csdo->Tfer.Type          = CO_CSDO_TRANSFER_NONE;
        csdo->Tfer.Abort         = 0;
        csdo->Tfer.Idx           = 0;
        csdo->Tfer.Sub           = 0;
        csdo->Tfer.Timeout       = 0;
        csdo->Tfer.OnDownload    = 0;
        csdo->Tfer.OnUpload      = 0;

        if (csdo->Tfer.Tmr >= 0) {
            tid = COTmrDelete(&csdo->Node->Tmr, csdo->Tfer.Tmr);
            if (tid < 0) {
                node->Error = CO_ERR_TMR_DELETE;
            } else {
                csdo->Tfer.Tmr   = -1;
            }
        }

        /* Release SDO client for next request */
        csdo->State = CO_CSDO_STATE_IDLE;
    }
}
