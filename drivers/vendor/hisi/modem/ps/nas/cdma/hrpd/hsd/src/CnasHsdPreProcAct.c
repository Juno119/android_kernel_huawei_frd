

/*****************************************************************************
  1 头文件包含
*****************************************************************************/
#include  "CnasHsdPreProcAct.h"
#include  "CnasHsdComFunc.h"
#include  "CnasHsdMntn.h"
#include  "CnasHsdSysAcqStrategy.h"
#include  "CnasHsdAvoidStrategy.h"
#include  "CnasHsdSndInternalMsg.h"
#include  "CnasHsdFsmMainTbl.h"
#include  "CnasHsdSndCas.h"
#include  "CnasHsdFsmSysAcqTbl.h"
#include  "cas_hrpd_airlinkmgmt_nas_pif.h"

#include  "CnasHsdSndMscc.h"

#include "CnasHsdSndHsm.h"

#include "CnasHsdSndRrm.h"
#include  "cas_hrpd_routeupdate_nas_pif.h"

#include "OmApi.h"

#ifdef  __cplusplus
#if  __cplusplus
extern "C"{
#endif
#endif

#define THIS_FILE_ID                    PS_FILE_ID_CNAS_HSD_PRE_PROC_ACT_C

#if (FEATURE_ON == FEATURE_UE_MODE_CDMA)

/*****************************************************************************
  2 全局变量定义
*****************************************************************************/

/*****************************************************************************
  3 函数定义
*****************************************************************************/
/*lint -save -e958*/


VOS_UINT32 CNAS_HSD_RcvSliceReverseProtectTimerExpired_PreProc(
    VOS_UINT32                          ulEventType,
    struct MsgCB                       *pstMsg
)
{
    VOS_UINT32                          ulCurSlice;
    VOS_UINT32                          ulRunSliceNum;
    VOS_UINT8                           ucIndex;
    CNAS_HSD_AVOID_FREQ_INFO_STRU      *pstAvoidFreqInfo;

    /* 获取当前系统的slice */
    ulCurSlice = CNAS_HSD_GetSystemSlice();

    /* 遍历avoid列表中所有的频点的到期slice */
    for (ucIndex = 0; ucIndex < CNAS_HSD_MAX_AVOID_FREQ_NUM; ucIndex++)
    {
        pstAvoidFreqInfo = CNAS_HSD_GetAvoidItemFromAvoidList(ucIndex);

        if (CNAS_HSD_AVOID_ITEM_UNUSED == pstAvoidFreqInfo->enUsedFlag)
        {
            continue;
        }

        if (ulCurSlice < pstAvoidFreqInfo->ulStartSlice)
        {
            /* 考虑反转问题 */
            ulRunSliceNum = CNAS_HSD_MAX_SLICE_VALUE - pstAvoidFreqInfo->ulStartSlice + ulCurSlice + 1;
        }
        else
        {
            ulRunSliceNum = ulCurSlice - pstAvoidFreqInfo->ulStartSlice;
        }

        /* 如果已经运行的slice数目大于等于到期的slice数目，从avoid频点列表中剔除该频点 */
        if (ulRunSliceNum >= pstAvoidFreqInfo->ulExpiredSliceNum)
        {
            CNAS_HSD_DeleteItemFromAvoidList(ucIndex);
            CNAS_HSD_DecreaseAvoidFreqUsedNum();
        }
    }

    /* 如果avoid list不为空，重新启动slice定时器 */
    if (0 != CNAS_HSD_GetAvoidFreqListUsedNum())
    {
        CNAS_HSD_StartTimer(TI_CNAS_HSD_SLICE_REVERSE_PROTECT_TIMER,
                            TI_CNAS_HSD_SLICE_REVERSE_PROTECT_TIMER_LEN);
    }

    CNAS_HSD_LogAvoidFreqList(CNAS_HSD_GetAvoidFreqListAddr());

    return VOS_TRUE;
}

/* 该消息在迭代15不再由HRUP发送，移至Network lost原因值中，后续需要作适应修改 */
#if 0

VOS_UINT32 CNAS_HSD_RcvTchAssignTimeoutInd_PreProc(
    VOS_UINT32                          ulEventType,
    struct MsgCB                       *pstMsg
)
{
    CNAS_HSD_REACQ_FALG_ENUM_UINT32     enReAcqFlag;

    enReAcqFlag = CNAS_HSD_NOT_NEED_REACQ;

    /* avoid模块处理 */
    CNAS_HSD_ProcAvoidEventInTchAssignTimeoutInd(&enReAcqFlag);

    /* 是否需要重新搜网 */
    if (CNAS_HSD_NEED_REACQ == enReAcqFlag)
    {
        CNAS_HSD_SetCurrFsmState(CNAS_HSD_L1_STA_INITIAL);

        CNAS_HSD_SndInternalSysAcqReq(CNAS_HSD_SYS_ACQ_SCENE_SWITCH_ON, 0, VOS_NULL_PTR);
    }

    return VOS_TRUE;
}
#endif

VOS_VOID CNAS_HSD_ProcHrpd1XSysAssn(CNAS_HSD_AT_STATUS_ENUM_UINT32 enHsdATStatus)
{
    CNAS_HSD_AT_STATUS_ENUM_UINT32      enLastStatus;
    CNAS_HSD_ASSN_RLST_ENUM_UINT32      enAssnRlst;
    CNAS_PRL_HRPD_SYSTEM_STRU          *pstHrpdSys;

    enLastStatus = CNAS_HSD_GetATStatus();

    /* 系统从非IDLE态到IDLE态才需要判断 */
    if ((CNAS_HSD_AT_STATUS_ENUM_IDLE == enLastStatus)
     || (CNAS_HSD_AT_STATUS_ENUM_IDLE != enHsdATStatus))
    {
        return;
    }

    if (CNAS_HSD_HRPD_SYSTEM_AVAILABLE != CNAS_HSD_GetHrpdSysStatus())
    {
        CNAS_WARNING_LOG(UEPS_PID_HSD, "CNAS_HSD_ProcHrpd1XSysAssn: Current state is incorrect.");

        /* 异常保护，直接返回 */
        return ;
    }

    /* 假如为锁频下就不用判断 */
    if (VOS_TRUE == CNAS_HSD_GetFreqLockMode())
    {
        return;
    }

    pstHrpdSys = CNAS_HSD_GetCampedHrpdSystemInfo();

    /*  获取当前1X和HRPD的关联关系 */
    enAssnRlst = CNAS_HSD_CheckHrpdFreqAssnWith1X(pstHrpdSys);

    /* 当前没有相关联的频点 */
    if (CNAS_HSD_NO_ASSN_FREQ == enAssnRlst)
    {
        CNAS_HSD_ClearHrpdSysInfo();

        CNAS_HSD_SetCurrFsmState(CNAS_HSD_L1_STA_OOC);

        CNAS_HSD_SndCasOocNtf();

        /* 上报搜网失败,通知MSCC切换到LTE */
        CNAS_HSD_SndMsccSysAcqInd(NAS_MSCC_PIF_ACQUIRED_RESULT_FAIL);

        OM_SetDrxTimerWakeSrc(CNAS_CCB_GetCdmaModeModemId(), VOS_RATMODE_HRPD);

        CNAS_HSD_LogDrxTimerStatus(VOS_TRUE);

        return;
    }

    /* 有关联频点，但和当前的HRPD系统不相关联 */
    if (CNAS_HSD_NO_ASSN_WITH_HRPD == enAssnRlst)
    {
        CNAS_HSD_SetCurrFsmState(CNAS_HSD_L1_STA_INITIAL);

        CNAS_HSD_SndInternalSysAcqReq(CNAS_HSD_SYS_ACQ_SCENE_SWITCH_ON, 0, VOS_NULL_PTR);
    }

    return;
}


VOS_UINT32 CNAS_HSD_RcvCasStatusInd_PreProc(
    VOS_UINT32                          ulEventType,
    struct MsgCB                       *pstMsg
)
{
    HSM_HSD_CAS_STATUS_IND_STRU        *pstCasStatusInd;
    CNAS_HSD_AT_STATUS_ENUM_UINT32      enHsdATStatus;

    pstCasStatusInd = (HSM_HSD_CAS_STATUS_IND_STRU*)pstMsg;

    enHsdATStatus   = CNAS_HSD_TransferCasStatusTypeToHsdFormat(pstCasStatusInd->enCasStatus);

    /* avoid模块处理 */
    CNAS_HSD_ProcAvoidEventInCasStatusInd(enHsdATStatus);

    CNAS_HSD_ProcHrpd1XSysAssn(enHsdATStatus);

    /* 保存AT当前状态 */
    CNAS_HSD_SaveATStatus(enHsdATStatus);


    CNAS_HSD_SndMsccHrpdCasStatusInd((HSD_MSCC_HRPD_CAS_STATUS_ENUM_UINT16)enHsdATStatus);


    return VOS_TRUE;
}


VOS_UINT32 CNAS_HSD_RcvPsStatusInd_PreProc(
    VOS_UINT32                          ulEventType,
    struct MsgCB                       *pstMsg
)
{
    CNAS_HSD_StopTimer(TI_CNAS_HSD_WAIT_RRM_STATUS_IND);

    CNAS_HSD_SndRrmDeRegisterInd(RRM_PS_TASK_TYPE_HRPD_NETWORK_SEARCH);

    return VOS_FALSE;
}


VOS_UINT32 CNAS_HSD_Rcv1xSysChgInd_PreProc(
    VOS_UINT32                          ulEventType,
    struct MsgCB                       *pstMsg
)
{
    MSCC_HSD_1X_SYS_CHANGE_IND_STRU    *pst1xSysIndMsg;
    CNAS_HSD_1X_SYSTEM_INFO_STRU       *pstHsd1xSysInfo;
    CNAS_PRL_1X_SYSTEM_STRU             st1xSrcSys;

    pst1xSysIndMsg = (MSCC_HSD_1X_SYS_CHANGE_IND_STRU *)pstMsg;

    pstHsd1xSysInfo = CNAS_HSD_Get1xSystemInfo();

    /* 1X搜网成功之后设置为AVAILABLE状态，保存当前的系统信息 */
    if (VOS_TRUE == pst1xSysIndMsg->ucIs1xAvail)
    {
        if (CNAS_HSD_1X_SYSTEM_AVAILABLE == pstHsd1xSysInfo->enStatus)
        {
            st1xSrcSys.usSid               = pst1xSysIndMsg->st1xSysInfo.usSid;
            st1xSrcSys.usNid               = pst1xSysIndMsg->st1xSysInfo.usNid;
            st1xSrcSys.stFreq.enBandClass  = pst1xSysIndMsg->st1xSysInfo.usBandClass;
            st1xSrcSys.stFreq.usChannel    = pst1xSysIndMsg->st1xSysInfo.usChannel;

            /* 假如新的1X网络系统和之前相同则不进行后续流程 */
            if (VOS_TRUE == CNAS_PRL_Is1xSysMatched(&st1xSrcSys,
                                                    &(pstHsd1xSysInfo->st1xSys)))
            {
                return VOS_TRUE;
            }
        }

        pstHsd1xSysInfo->enStatus = CNAS_HSD_1X_SYSTEM_AVAILABLE;

        /* 这里是否需要考虑前后1X系统不一致的情况 */
        pstHsd1xSysInfo->st1xSys.stFreq.enBandClass = pst1xSysIndMsg->st1xSysInfo.usBandClass;
        pstHsd1xSysInfo->st1xSys.stFreq.usChannel   = pst1xSysIndMsg->st1xSysInfo.usChannel;

        pstHsd1xSysInfo->st1xSys.usNid              = pst1xSysIndMsg->st1xSysInfo.usNid;
        pstHsd1xSysInfo->st1xSys.usSid              = pst1xSysIndMsg->st1xSysInfo.usSid;
    }
    else if (CNAS_HSD_1X_SYSTEM_AVAILABLE != pstHsd1xSysInfo->enStatus)
    {
        pstHsd1xSysInfo->enStatus = CNAS_HSD_1X_SYSTEM_UNAVAILABLE;

        NAS_MEM_SET_S(pstHsd1xSysInfo, sizeof(CNAS_HSD_1X_SYSTEM_INFO_STRU), 0, sizeof(CNAS_HSD_1X_SYSTEM_INFO_STRU));
    }
    else
    {
        ;
    }

    return VOS_FALSE;
}


VOS_UINT32 CNAS_HSD_RcvMsccQryHrpdSysInfoReq_PreProc(
    VOS_UINT32                          ulEventType,
    struct MsgCB                       *pstMsg
)
{
    MSCC_HSD_QRY_HRPD_SYS_INFO_REQ_STRU *pHrpdSysInfo;
    VOS_UINT8                            ucHrpdSysNum;
    MSCC_HSD_HRPD_SYS_INFO_STRU          astHrpdSysInfoList[MSCC_HSD_BG_HRPD_MAX_SYS_NUM];

    NAS_MEM_SET_S(astHrpdSysInfoList,
                  sizeof(MSCC_HSD_HRPD_SYS_INFO_STRU) * MSCC_HSD_BG_HRPD_MAX_SYS_NUM,
                  0,
                  sizeof(MSCC_HSD_HRPD_SYS_INFO_STRU) * MSCC_HSD_BG_HRPD_MAX_SYS_NUM);

    pHrpdSysInfo    =   (MSCC_HSD_QRY_HRPD_SYS_INFO_REQ_STRU *)pstMsg;

    if (MSCC_HSD_SYS_TYPE_CDMA2000_AI == pHrpdSysInfo->enSysType)
    {
        CNAS_HSD_QRY_HrpdSysInfo(pHrpdSysInfo,
                                 &ucHrpdSysNum,
                                 astHrpdSysInfoList,
                                 CNAS_HSD_GetPriClassWithTypeAI);

    }
    else if (MSCC_HSD_SYS_TYPE_CDMA2000_HRPD== pHrpdSysInfo->enSysType)
    {
        CNAS_HSD_QRY_HrpdSysInfo(pHrpdSysInfo,
                                 &ucHrpdSysNum,
                                 astHrpdSysInfoList,
                                 CNAS_HSD_GetPriClassWithTypeHRPD);
    }
    else
    {
        ucHrpdSysNum = 0;
    }

    CNAS_HSD_SndMsccHrpdSysInfoCnf(ucHrpdSysNum, astHrpdSysInfoList);

    return VOS_TRUE;
}


VOS_UINT32 CNAS_HSD_RcvMsccDisableLteNtf_PreProc(
    VOS_UINT32                          ulEventType,
    struct MsgCB                       *pstMsg
)
{
    /* HSD给CAS透传DISABLE消息 */
    CNAS_HSD_SndCasDisableLteNtf();

    return VOS_TRUE;
}


VOS_UINT32 CNAS_HSD_RcvMsccEnableLteNtf_PreProc(
    VOS_UINT32                          ulEventType,
    struct MsgCB                       *pstMsg
)
{
    /* HSD给CAS透传ENABLE LTE消息 */
    CNAS_HSD_SndCasEnableLteNtf();

    return VOS_TRUE;
}



VOS_UINT32 CNAS_HSD_RcvMsccBgSearchReq_PreProc(
    VOS_UINT32                          ulEventType,
    struct MsgCB                       *pstMsg
)
{
    CNAS_HSD_FSM_ID_ENUM_UINT32         enFsmId;
    VOS_UINT32                          ulState;
    CAS_CNAS_HRPD_BSR_LTE_CNF_STRU      stBsrLteCnf = {0};

    NAS_MEM_SET_S(&stBsrLteCnf, sizeof(stBsrLteCnf), 0, sizeof(stBsrLteCnf));

    enFsmId              = CNAS_HSD_GetCurrFsmId();
    ulState              = CNAS_HSD_GetFsmTopState();

    if ((CNAS_HSD_FSM_L1_MAIN == enFsmId)
     && (CNAS_HSD_L1_STA_IDLE == ulState))
    {
        return VOS_FALSE;
    }
    else
    {
        /* 直接回复abort */
        stBsrLteCnf.ucRslt  = CNAS_CAS_HRPD_BSR_RSLT_ABORT;
        CNAS_HSD_SndMsccBgSearchCnf(&stBsrLteCnf);
    }

    return VOS_TRUE;
}


VOS_UINT32 CNAS_HSD_RcvCasFreqListReq_PreProc(
    VOS_UINT32                          ulEventType,
    struct MsgCB                       *pstMsg
)
{
    CAS_CNAS_HRPD_BSR_FREQ_LIST_QUERY_REQ_STRU             *pstFreqListReq;
    CNAS_PRL_FREQENCY_CHANNEL_STRU                          astFreqList[CNAS_CAS_HRPD_MAX_FREQ_NUM];
    VOS_UINT32                                              ulFreqNum;

    NAS_MEM_SET_S(astFreqList, sizeof(astFreqList), 0, sizeof(astFreqList));

    pstFreqListReq = (CAS_CNAS_HRPD_BSR_FREQ_LIST_QUERY_REQ_STRU *)pstMsg;

    if (PS_TRUE == pstFreqListReq->enFirstQueryFlag)
    {
        /* 按照开机场景构造频点列表 */
        CNAS_HSD_BuildHrpdScanList_CasReq(CNAS_HSD_SYS_ACQ_SCENE_SWITCH_ON);
    }

    ulFreqNum = CNAS_HSD_GetScanListSameLevelFreq(CNAS_CAS_HRPD_MAX_FREQ_NUM, astFreqList);

    if (0 == ulFreqNum)
    {
        CNAS_WARNING_LOG(UEPS_PID_HSD, "CNAS_HSD_RcvCasFreqListReq_PreProc: ulFreqNum is 0.");
    }

    /* 给CAS发送ID_CNAS_CAS_HRPD_FREQ_LIST_QUERY_CNF，即使构造的频点个数为0，也需要回复此请求 */
    CNAS_HSD_SndCasFreqListCnf(ulFreqNum, astFreqList);

    CNAS_HSD_RefreshScanListIndex((VOS_UINT16)ulFreqNum);

    return VOS_TRUE;

}


VOS_UINT32 CNAS_HSD_RcvCasOhmNotCurrentInd_PreProc(
    VOS_UINT32                          ulEventType,
    struct MsgCB                       *pstMsg
)
{
    /* Send indication to HSM */
    CNAS_HSD_SndHsmOhmNotCurrentInd();

    return VOS_TRUE;
}

#ifdef DMT

VOS_UINT32 CNAS_HSD_RcvHsdTestCfgREQ_PreProc(
    VOS_UINT32                          ulEventType,
    struct MsgCB                       *pstMsg
)
{
    CNAS_HSD_HSD_TEST_CFG_REQ_STRU     *pstCfgReq = VOS_NULL_PTR;

    pstCfgReq = (CNAS_HSD_HSD_TEST_CFG_REQ_STRU*)pstMsg;

    g_ulHsdCurSlice = pstCfgReq->ulCurSlice;

    return VOS_TRUE;
}
#endif


VOS_UINT32 CNAS_HSD_RcvMsccSysCfgSet_PreProc(
    VOS_UINT32                          ulEventType,
    struct MsgCB                       *pstMsg
)
{
    MSCC_HSD_SYS_CFG_REQ_STRU          *pstSysCfgReq = VOS_NULL_PTR;

    pstSysCfgReq = (MSCC_HSD_SYS_CFG_REQ_STRU *)pstMsg;

    CNAS_HSD_SetSysModeType(CNAS_HSD_CheckHybridMode(pstSysCfgReq->ucSuppRatNum, &pstSysCfgReq->aenRatMode[0]));

    /* 处理中国电信定制事件 */
    CNAS_HSD_ProcessCTCCCustomize();

    CNAS_HSD_SetSysCfgFlag(VOS_TRUE);

    CNAS_HSD_SndCasSysCfgReq(pstSysCfgReq);

    CNAS_HSD_InitOocCtxInfo();

    return VOS_TRUE;
}


VOS_UINT32 CNAS_HSD_RcvCasSysCfgCnf_PreProc(
    VOS_UINT32                          ulEventType,
    struct MsgCB                       *pstMsg
)
{
    CAS_CNAS_HRPD_SYS_CFG_CNF_STRU     *pstSysCfgCnf = VOS_NULL_PTR;

    pstSysCfgCnf = (CAS_CNAS_HRPD_SYS_CFG_CNF_STRU *)pstMsg;

    CNAS_HSD_SetSysCfgFlag(VOS_FALSE);

    CNAS_HSD_SndMsccSysCfgCnf(pstSysCfgCnf);

    return VOS_TRUE;
}


VOS_UINT32 CNAS_HSD_RcvCasNetworkLostIndProc_PreProc(
    VOS_UINT32                          ulEventType,
    struct MsgCB                       *pstMsg
)
{
    CAS_CNAS_HRPD_NETWORK_LOST_IND_STRU                    *pstCasNetworkLostMsg;
    CNAS_HSD_NETWORK_LOST_SYS_RECORD_STRU                  *pstNetwkLostSysRec;
    CNAS_HSD_HISTORY_SYS_LIST_STRU                         *pstHistorySys;
    VOS_UINT32                                              ulIsChange;
    CNAS_HSD_SYS_MODE_TYPD_ENUM_UINT32                      enSysMode;

    pstCasNetworkLostMsg = (CAS_CNAS_HRPD_NETWORK_LOST_IND_STRU*)pstMsg;

    pstHistorySys        = CNAS_HSD_GetHistorySysList();
    pstNetwkLostSysRec   = CNAS_HSD_GetNetwkLostSysRec();
    enSysMode            = CNAS_HSD_GetModeType();

    /* 非混合模式下不对signal fade进行avoid处理 */
    if (CNAS_HSD_SYS_MODE_NONHYBRID == enSysMode)
    {
        return VOS_FALSE;
    }

    /* 成功驻留过后才进行处理 */
    if (0 != pstHistorySys->ulTotalNum)
    {
        /* 非no rf和abnormal 的场景才进行avoid处理 */
        if ((CAS_CNAS_HRPD_NETWORK_LOST_NO_RF    != pstCasNetworkLostMsg->enNetworkLostReason)
         && (CAS_CNAS_HRPD_NETWORK_LOST_ABNORMAL != pstCasNetworkLostMsg->enNetworkLostReason))
        {
            /* 是否在同一系统上丢网 */
            ulIsChange = CNAS_HSD_IsHrpdSysChange(&(pstHistorySys->stCampedHrpdSysInfo[0]),
                                                  &(pstNetwkLostSysRec->stHrpdSys));

            if (VOS_TRUE == ulIsChange)
            {
                NAS_MEM_CPY_S(&(pstNetwkLostSysRec->stHrpdSys),
                              sizeof(CNAS_PRL_HRPD_SYSTEM_STRU),
                              &(pstHistorySys->stCampedHrpdSysInfo[0]),
                              sizeof(CNAS_PRL_HRPD_SYSTEM_STRU));

                pstNetwkLostSysRec->ucNetwkLostCnt = 0x01;

                pstNetwkLostSysRec->ulLastRecSlice = CNAS_HSD_GetSystemSlice();
            }
            else
            {
                CNAS_HSD_AvoidNetwkLostProc(pstNetwkLostSysRec);
            }

        }
    }
    else
    {}

    return VOS_FALSE;
}


VOS_VOID CNAS_HSD_AvoidNetwkLostProc(
    CNAS_HSD_NETWORK_LOST_SYS_RECORD_STRU                  *pstNetwkLostSysRec
)
{
    VOS_UINT32                          ulCurSlice;
    VOS_UINT32                          ulInterSlice;
    VOS_UINT32                          ulMaxInterSlice;

    ulMaxInterSlice     = CNAS_HSD_TIMER_LEN_20_SEC * CNAS_HSD_GetSystemSliceNumOfOneSecond();

    ulCurSlice  = CNAS_HSD_GetSystemSlice();

    if (ulCurSlice < pstNetwkLostSysRec->ulLastRecSlice)
    {
        /* 考虑反转问题 */
        ulInterSlice = CNAS_HSD_MAX_SLICE_VALUE - pstNetwkLostSysRec->ulLastRecSlice + ulCurSlice + 1;
    }
    else
    {
        ulInterSlice = ulCurSlice - pstNetwkLostSysRec->ulLastRecSlice;
    }

    /* 更新时间 */
    pstNetwkLostSysRec->ulLastRecSlice = ulCurSlice;

    if (ulInterSlice > ulMaxInterSlice)
    {
        pstNetwkLostSysRec->ucNetwkLostCnt = 0x01;
    }
    else
    {
        pstNetwkLostSysRec->ucNetwkLostCnt++;

        /* 连续丢网超过3次 */
        if (pstNetwkLostSysRec->ucNetwkLostCnt >= CNAS_HSD_AVOID_NETWORK_LOST_MAX_CNT)
        {
            CNAS_HSD_ProcAvoidEventInNetwkLostInd(&(pstNetwkLostSysRec->stHrpdSys.stFreq));
        }
    }

    return;
}



VOS_UINT32 CNAS_HSD_RcvMsccCFreqLockSetNtf_PreProc(
    VOS_UINT32                          ulEventType,
    struct MsgCB                       *pstMsg
)
{
    MSCC_HSD_CFREQ_LOCK_NTF_STRU       *pstCFreqLockNtf         = VOS_NULL_PTR;
    CNAS_HSD_CFREQ_LOCK_SET_PARA_STRU  *pstCFreqLockSetPara     = VOS_NULL_PTR;
    CNAS_PRL_FREQENCY_CHANNEL_STRU      stLockFreq;
    CNAS_HSD_FSM_ID_ENUM_UINT32         enFsmId;
    VOS_UINT32                          ulState;

    pstCFreqLockNtf = (MSCC_HSD_CFREQ_LOCK_NTF_STRU *)pstMsg;

    /* 发送给CAS */
    if (VOS_TRUE != CNAS_HSD_SndCasCFreqLockNtf(pstCFreqLockNtf))
    {
        return VOS_TRUE;
    }

    pstCFreqLockSetPara                  = CNAS_HSD_GetFreqLockAddr();

    if (NAS_MSCC_PIF_CFREQ_LOCK_SET_MODE_ENABLE == pstCFreqLockNtf->enFreqLockMode)
    {
        pstCFreqLockSetPara->ucFreqLockMode  = VOS_TRUE;
    }
    else
    {
        pstCFreqLockSetPara->ucFreqLockMode  = VOS_FALSE;
    }
    pstCFreqLockSetPara->usHrpdBandClass = pstCFreqLockNtf->usHrpdBandClass;
    pstCFreqLockSetPara->usHrpdFreq      = pstCFreqLockNtf->usHrpdFreq;
    pstCFreqLockSetPara->usHrpdPn        = pstCFreqLockNtf->usHrpdPn;

    if (VOS_TRUE == pstCFreqLockSetPara->ucFreqLockMode)
    {
        enFsmId = CNAS_HSD_GetCurrFsmId();
        ulState = CNAS_HSD_GetFsmTopState();

        if (CNAS_HSD_FSM_L1_MAIN == enFsmId)
        {
            if (CNAS_HSD_L1_STA_IDLE == ulState)
            {
                stLockFreq.enBandClass  = pstCFreqLockSetPara->usHrpdBandClass;
                stLockFreq.usChannel    = pstCFreqLockSetPara->usHrpdFreq;

                if (VOS_FALSE == CNAS_PRL_CmpFreqValue(&(CNAS_HSD_GetCampedHrpdSystemInfo()->stFreq), &stLockFreq))
                {
                    /* IDLE状态下触发搜网 */
                    CNAS_HSD_SetCurrFsmState(CNAS_HSD_L1_STA_INITIAL);

                    CNAS_HSD_SndInternalSysAcqReq(CNAS_HSD_SYS_ACQ_SCENE_CFREQ_LOCK, 0, VOS_NULL_PTR);
                }
            }
            else if (CNAS_HSD_L1_STA_OOC == ulState)
            {
                /* OOC状态下触发搜网 */
                CNAS_HSD_SetCurrFsmState(CNAS_HSD_L1_STA_INITIAL);

                CNAS_HSD_SndInternalSysAcqReq(CNAS_HSD_SYS_ACQ_SCENE_CFREQ_LOCK, 0, VOS_NULL_PTR);
            }
        }
    }

    return VOS_TRUE;

}


VOS_UINT32 CNAS_HSD_RcvNoRFProtectTimerExpired_PreProc(
    VOS_UINT32                          ulEventType,
    struct MsgCB                       *pstMsg
)
{
    CNAS_HSD_SYS_ACQ_SCENE_ENUM_UINT32  enScene;

    if (VOS_FALSE == CNAS_HSD_GetHrpdRfAvailFlg())
    {
        enScene = CNAS_HSD_GetNoRfScene();

        if (CNAS_HSD_SYS_ACQ_SCENE_BUTT >= enScene)
        {
            CNAS_HSD_SetNoRfScene(CNAS_HSD_SYS_ACQ_SCENE_BUTT);

            /* 设置L1状态机进入INITIAL状态 */
            CNAS_HSD_SetCurrFsmState(CNAS_HSD_L1_STA_INITIAL);

            CNAS_HSD_SndInternalSysAcqReq(CNAS_HSD_SYS_ACQ_SCENE_SWITCH_ON, 0, VOS_NULL_PTR);
        }
    }

    return VOS_TRUE;
}
#if 0

VOS_UINT32 CNAS_HSD_RcvMsccDataCallStartNtf_PreProc(
    VOS_UINT32                          ulEventType,
    struct MsgCB                       *pstMsg
)
{
    return VOS_TRUE;
}


VOS_UINT32 CNAS_HSD_RcvMsccDataCallEndNtf_PreProc(
    VOS_UINT32                          ulEventType,
    struct MsgCB                       *pstMsg
)
{
    return VOS_TRUE;
}


VOS_UINT32 CNAS_HSD_RcvMsccDataCallSuccNtf_PreProc(
    VOS_UINT32                          ulEventType,
    struct MsgCB                       *pstMsg
)
{
    return VOS_TRUE;
}
#endif


VOS_UINT32 CNAS_HSD_RcvCasSuspendInd_PreProc(
    VOS_UINT32                          ulEventType,
    struct MsgCB                       *pstMsg
)
{
    CNAS_HSD_FSM_ID_ENUM_UINT32         enFsmId;
    VOS_UINT32                          ulState;

    enFsmId              = CNAS_HSD_GetCurrFsmId();
    ulState              = CNAS_HSD_GetFsmTopState();

    /* 将SysCfg消息发送给CAS后还没接受到CAS的回复 */
    if (VOS_TRUE == CNAS_HSD_GetSysCfgFlag())
    {
        CNAS_HSD_SndCasSuspendRsp(CAS_CNAS_HRPD_RSLT_FAIL);

        return VOS_TRUE;
    }

    /* 若在背景搜状态机，先进状态机缓存，待退出背景搜状态机后在层1 IDLE态处理 */
    if (CNAS_HSD_FSM_BG_SRCH == enFsmId)
    {
        return VOS_FALSE;
    }

    /* 如果当前状态机在idle态，回复成功，并进状态机处理，否则只回复失败即可 */
    if ((CNAS_HSD_FSM_L1_MAIN == enFsmId)
     && (CNAS_HSD_L1_STA_IDLE == ulState))
    {
        CNAS_HSD_SndCasSuspendRsp(CAS_CNAS_HRPD_RSLT_SUCC);

        return VOS_FALSE;
    }
    else
    {
        CNAS_HSD_SndCasSuspendRsp(CAS_CNAS_HRPD_RSLT_FAIL);

        return VOS_TRUE;
    }
}



VOS_UINT32 CNAS_HSD_RcvMsccHdrCsqSetReq_PreProc(
    VOS_UINT32                          ulEventType,
    struct MsgCB                       *pstMsg
)
{
    MSCC_HSD_HDR_CSQ_SET_REQ_STRU      *psrHdrCsqSetReq;

    psrHdrCsqSetReq = (MSCC_HSD_HDR_CSQ_SET_REQ_STRU*)pstMsg;

    /* 将set req 发送給cas */
    if (VOS_ERR == CNAS_HSD_SndCasHdrCsqSetReq(psrHdrCsqSetReq))
    {

        /* 給MSCC 回复SET 失败 */
        CNAS_HSD_SndMsccHdrCsqSetCnf(NAS_MSCC_PIF_CDMACSQ_SET_RESULT_FAIL);
    }

    return VOS_TRUE;
}


VOS_UINT32 CNAS_HSD_RcvCasHdrCsqSetCnf_PreProc(
    VOS_UINT32                          ulEventType,
    struct MsgCB                       *pstMsg
)
{
    CAS_CNAS_HRPD_SET_SIGNAL_QUALITY_CNF_STRU              *psrHdrCsqSetCnf;

    psrHdrCsqSetCnf = (CAS_CNAS_HRPD_SET_SIGNAL_QUALITY_CNF_STRU*)pstMsg;

    /* 給MSCC 回复SET CNF */
    CNAS_HSD_SndMsccHdrCsqSetCnf(psrHdrCsqSetCnf->enRslt);

    return VOS_TRUE;
}


VOS_UINT32 CNAS_HSD_RcvMsccHdrCsqQualityInd_PreProc(
    VOS_UINT32                          ulEventType,
    struct MsgCB                       *pstMsg
)
{
    CAS_CNAS_HRPD_SIGNAL_QUALITY_IND_STRU              *psrHdrCsqInd;

    psrHdrCsqInd = (CAS_CNAS_HRPD_SIGNAL_QUALITY_IND_STRU*)pstMsg;

    /* 給MSCC 发送HDR CSQ IND*/
    CNAS_HSD_SndMsccHdrCsqQualityInd(psrHdrCsqInd->sHrpdRssi,
                                     psrHdrCsqInd->sHrpdSinr,
                                     psrHdrCsqInd->sHrpdEcIo);

    return VOS_TRUE;
}


#if (FEATURE_PTM == FEATURE_ON)

VOS_VOID CNAS_HSD_ErrLogReportHrpdOosSearchInfo(
    OM_ERR_LOG_REPORT_REQ_STRU         *pstRcvOmErrLogReportReq
)
{
    VOS_UINT32                                              ulLength;
    NAS_ERR_LOG_HRPD_OOS_SEARCH_STRU                       *pstHrpdOosSearch;
    NAS_ERRLOG_CCB_HRPD_OOS_SEARCH_STRU                    *pstErrLogCcbHrpdOosSearch = VOS_NULL_PTR;

    ulLength = sizeof(NAS_ERR_LOG_HRPD_OOS_SEARCH_STRU);

    pstHrpdOosSearch =  (NAS_ERR_LOG_HRPD_OOS_SEARCH_STRU *)PS_MEM_ALLOC(UEPS_PID_HSD, ulLength);

    if (VOS_NULL_PTR == pstHrpdOosSearch)
    {
        CNAS_ERROR_LOG(UEPS_PID_HSD, "CNAS_HSD_ErrLogReportHrpdOosSearchInfo:ERROR:Alloc Mem Fail.");
        return;
    }

    NAS_MEM_SET_S(pstHrpdOosSearch, ulLength, 0x00, ulLength);

    /* 获取需要上报的Hrpd搜网记录 */
    pstErrLogCcbHrpdOosSearch = NAS_ERRLOG_CCB_GetHrpdOosSearchCtxAddr();

    /* 获取20条全部记录，即使可能并未存在记录 */
    pstHrpdOosSearch->ulSearchRecordCnt = pstErrLogCcbHrpdOosSearch->ulSearchRecordCnt;

    NAS_MEM_CPY_S(pstHrpdOosSearch->astSearchRecord,
                  NAS_ERR_LOG_HRPD_OOS_MAX_SEARCH_RECORD * sizeof(NAS_ERR_LOG_HRPD_OOS_SEARCH_RECORD_STRU),
                  pstErrLogCcbHrpdOosSearch->astHrpdOosSearchRecord,
                  NAS_ERR_LOG_HRPD_OOS_MAX_SEARCH_RECORD * sizeof(NAS_ERR_LOG_HRPD_OOS_SEARCH_RECORD_STRU));

    /* 填充OM_ERR_LOG_HEADER_STRU stHeader */
    CNAS_COMM_BULID_ERRLOG_HEADER_INFO(&pstHrpdOosSearch->stHeader,
                                       CNAS_CCB_GetCdmaModeModemId(),
                                       NAS_ERR_LOG_ALM_HRPD_OOS_SEARCH,
                                       NAS_GetErrLogAlmLevel(NAS_ERR_LOG_ALM_HRPD_OOS_SEARCH),
                                       VOS_GetSlice(),
                                       (ulLength - sizeof(OM_ERR_LOG_HEADER_STRU)));

    /* 应答OM查询Hrpd搜网信息 */
    NAS_ERRLOG_CCB_SndAcpuOmFaultErrLogCnf(pstHrpdOosSearch,
                                           ulLength,
                                           pstRcvOmErrLogReportReq->ucFaultID,
                                           (VOS_UINT8)NAS_ERR_LOG_ALM_HRPD_OOS_SEARCH,
                                           UEPS_PID_HSD);


    PS_MEM_FREE(UEPS_PID_HSD, pstHrpdOosSearch);

    return;
}


VOS_UINT32 CNAS_HSD_RcvAcpuOmErrlogRptReq_PreProc(
    VOS_UINT32                          ulEventType,
    struct MsgCB                       *pstMsg
)
{
    OM_ERR_LOG_REPORT_REQ_STRU         *pstRcvOmErrLogReportReq = VOS_NULL_PTR;

    pstRcvOmErrLogReportReq = (OM_ERR_LOG_REPORT_REQ_STRU*)pstMsg;

    if ((NAS_ERR_LOG_ALM_HRPD_OOS_SEARCH == pstRcvOmErrLogReportReq->ucAlarmID)
     && (FAULT_ID_HRPD_OR_LTE_OOS_RECOVERY == pstRcvOmErrLogReportReq->ucFaultID))
    {
        /* AlarmId和FaultId符合需要处理 */
        CNAS_HSD_ErrLogReportHrpdOosSearchInfo(pstRcvOmErrLogReportReq);

        /* 应答OM后清空Hrpd搜网信息 */
        NAS_ERRLOG_CCB_InitHrpdOosSearchCtx();
    }

    return VOS_TRUE;
}
#endif


VOS_UINT32 CNAS_HSD_RcvTiSysAcqNoRfProtectTimerExpired_PreProc(
    VOS_UINT32                          ulEventType,
    struct MsgCB                       *pstMsg
)
{
    CNAS_HSD_SetSysAcqNoRfProtectTimerExpiredFlag(VOS_TRUE);

    return VOS_TRUE;
}

/*lint -restore*/

#endif

#ifdef __cplusplus
#if __cplusplus
}
#endif /* __cpluscplus */
#endif /* __cpluscplus */



