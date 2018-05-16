#include "ReactorUDPHander.h"

CReactorUDPHander::CReactorUDPHander(void)
{
    m_pPacketParse        = NULL;
    m_u4RecvPacketCount   = 0;
    m_u4SendPacketCount   = 0;
    m_u4RecvSize          = 0;
    m_u4SendSize          = 0;
    m_u4PacketParseInfoID = 0;
}

CReactorUDPHander::~CReactorUDPHander(void)
{
}

int CReactorUDPHander::OpenAddress(const ACE_INET_Addr& AddrRemote, ACE_Reactor* pReactor)
{
    if(m_skRemote.open(AddrRemote) == -1)
    {
        OUR_DEBUG((LM_ERROR, "[CReactorUDPHander::OpenAddress]Open error(%d).\n", errno));
        return -1;
    }

    reactor(pReactor);

    return Init_Open_Address(AddrRemote);
}

int CReactorUDPHander::OpenAddress(const ACE_INET_Addr& AddrRemote)
{
    if (m_skRemote.open(AddrRemote) == -1)
    {
        OUR_DEBUG((LM_ERROR, "[CReactorUDPHander::OpenAddress]Open error(%d).\n", errno));
        return -1;
    }

    return Init_Open_Address(AddrRemote);
}



int CReactorUDPHander::Run_Open(ACE_Reactor* pReactor)
{
    reactor(pReactor);

    if (-1 == this->reactor()->register_handler(this, ACE_Event_Handler::READ_MASK))
    {
        OUR_DEBUG((LM_ERROR, "[CReactorUDPHander::OpenAddress] Addr is register_handler error(%d).\n", errno));
        return -1;
    }

    return 0;
}

void CReactorUDPHander::Close()
{
    ACE_Reactor_Mask close_mask = ACE_Event_Handler::ALL_EVENTS_MASK | ACE_Event_Handler::DONT_CALL;
    this->reactor()->remove_handler(this, close_mask);
    m_skRemote.close();
}

ACE_HANDLE CReactorUDPHander::get_handle(void) const
{
    return m_skRemote.get_handle();
}

int CReactorUDPHander::handle_input(ACE_HANDLE fd)
{
    if(fd == ACE_INVALID_HANDLE)
    {
        OUR_DEBUG((LM_ERROR, "[CReactorUDPHander::handle_input]fd is ACE_INVALID_HANDLE.\n"));
        return -1;
    }

    char szBuff[MAX_UDP_PACKET_LEN] = {'\0'};

    int nDataLen = (int)m_skRemote.recv(szBuff, MAX_UDP_PACKET_LEN, m_addrRemote);

    if(nDataLen > 0)
    {
        if (false == CheckMessage(szBuff, (uint32)nDataLen))
        {
            OUR_DEBUG((LM_INFO, "[CReactorUDPHander::handle_input]CheckMessage fail.\n"));
        }

        m_pPacketParse = App_PacketParsePool::instance()->Create(__FILE__, __LINE__);
    }

    return 0;
}

int CReactorUDPHander::handle_close(ACE_HANDLE handle, ACE_Reactor_Mask close_mask)
{
    if(handle == ACE_INVALID_HANDLE)
    {
        OUR_DEBUG((LM_ERROR, "[CReactorUDPHander::handle_close]close_mask = %d.\n", (uint32)close_mask));
    }

    Close();
    return 0;
}

void CReactorUDPHander::SetPacketParseInfoID(uint32 u4PacketParseInfoID)
{
    m_u4PacketParseInfoID = u4PacketParseInfoID;
}

bool CReactorUDPHander::SendMessage(char*& pMessage, uint32 u4Len, const char* szIP, int nPort, bool blHead, uint16 u2CommandID, bool blDlete)
{
    ACE_Message_Block* pMbData = NULL;
    ACE_INET_Addr AddrRemote;

    _Send_Message_Param obj_Send_Message_Param;
    obj_Send_Message_Param.m_u4PacketParseInfoID = m_u4PacketParseInfoID;
    obj_Send_Message_Param.m_blDlete             = blDlete;
    obj_Send_Message_Param.m_blHead              = blHead;
    obj_Send_Message_Param.m_nPort               = nPort;
    obj_Send_Message_Param.m_pIP                 = (char*)szIP;
    obj_Send_Message_Param.m_u2CommandID         = u2CommandID;
    obj_Send_Message_Param.m_u4Len               = u4Len;

    bool blState = Udp_Common_Send_Message(obj_Send_Message_Param,
                                           AddrRemote,
                                           pMessage,
                                           pMbData);

    if (true == blState)
    {
        int nSize = (int)m_skRemote.send(pMbData->rd_ptr(), pMbData->length(), AddrRemote);

        if ((uint32)nSize == pMbData->length())
        {
            m_atvOutput = ACE_OS::gettimeofday();
            m_u4SendSize += u4Len;
            m_u4SendPacketCount++;

            //统计发送信息
            m_CommandAccount.SaveCommandData(u2CommandID, (uint32)nPort, PACKET_UDP, u4Len, COMMAND_TYPE_OUT);

            //释放发送体
            App_MessageBlockManager::instance()->Close(pMbData);

            return true;
        }
        else
        {
            OUR_DEBUG((LM_ERROR, "[CProactorUDPHandler::SendMessage]send error(%d).\n", errno));

            //释放发送体
            App_MessageBlockManager::instance()->Close(pMbData);

            return false;
        }
    }

    return true;
}

_ClientConnectInfo CReactorUDPHander::GetClientConnectInfo()
{
    _ClientConnectInfo ClientConnectInfo;
    ClientConnectInfo.m_blValid       = true;
    ClientConnectInfo.m_u4ConnectID   = 0;
    ClientConnectInfo.m_u4AliveTime   = 0;
    ClientConnectInfo.m_u4BeginTime   = (uint32)m_atvInput.sec();
    ClientConnectInfo.m_u4AllRecvSize = m_u4RecvSize;
    ClientConnectInfo.m_u4AllSendSize = m_u4SendSize;
    ClientConnectInfo.m_u4RecvCount   = m_u4RecvPacketCount;
    ClientConnectInfo.m_u4SendCount   = m_u4SendPacketCount;
    return ClientConnectInfo;
}

bool CReactorUDPHander::CheckMessage(const char* pData, uint32 u4Len)
{
    ACE_Time_Value tvCheck = ACE_OS::gettimeofday();

    if(NULL == m_pPacketParse || NULL == pData)
    {
        return false;
    }

    if(App_PacketParseLoader::instance()->GetPacketParseInfo(m_u4PacketParseInfoID)->m_u1PacketParseType == PACKET_WITHHEAD)
    {
        m_pPacketParse->SetPacket_Head_Src_Length(App_PacketParseLoader::instance()->GetPacketParseInfo(m_u4PacketParseInfoID)->m_u4OrgLength);

        if(u4Len < m_pPacketParse->GetPacketHeadSrcLen())
        {
            return false;
        }

        //将完整的数据包转换为PacketParse对象
        ACE_Message_Block* pMBHead = App_MessageBlockManager::instance()->Create(m_pPacketParse->GetPacketHeadSrcLen());
        memcpy_safe((char* )pData, m_pPacketParse->GetPacketHeadSrcLen(), (char* )pMBHead->wr_ptr(), m_pPacketParse->GetPacketHeadSrcLen());
        pMBHead->wr_ptr(m_pPacketParse->GetPacketHeadLen());

        bool blRet = Udp_Common_Recv_Head(pMBHead, m_pPacketParse, m_u4PacketParseInfoID, u4Len);

        if (false == blRet)
        {
            App_MessageBlockManager::instance()->Close(pMBHead);
            App_PacketParsePool::instance()->Delete(m_pPacketParse);
            return false;
        }

        //如果包含包体
        if(m_pPacketParse->GetPacketBodySrcLen() > 0)
        {
            char* pBody = (char* )(&pData[0] + m_pPacketParse->GetPacketHeadSrcLen());
            ACE_Message_Block* pMBBody = App_MessageBlockManager::instance()->Create(m_pPacketParse->GetPacketBodyLen());
            memcpy_safe(pBody, m_pPacketParse->GetPacketBodyLen(), (char* )pMBBody->wr_ptr(), m_pPacketParse->GetPacketBodyLen());
            pMBBody->wr_ptr(m_pPacketParse->GetPacketBodySrcLen());

            bool blStateBody = Udp_Common_Recv_Body(pMBBody, m_pPacketParse, m_u4PacketParseInfoID);

            if (false == blStateBody)
            {
                App_MessageBlockManager::instance()->Close(pMBHead);
                App_MessageBlockManager::instance()->Close(pMBBody);
                App_PacketParsePool::instance()->Delete(m_pPacketParse);
                return false;
            }
        }

        //处理数据包
        if (false == Udp_Common_Send_WorkThread(m_pPacketParse, m_addrRemote, m_addrLocal, tvCheck))
        {
            return false;
        }
    }
    else
    {
        ACE_Message_Block* pMbData = App_MessageBlockManager::instance()->Create(u4Len);
        memcpy_safe((char* )pData, u4Len, (char* )pMbData->wr_ptr(), u4Len);
        pMbData->wr_ptr(u4Len);

        //以数据流处理
        if (false == Udp_Common_Recv_Stream(pMbData, m_pPacketParse, m_u4PacketParseInfoID))
        {
            App_PacketParsePool::instance()->Delete(m_pPacketParse);
            return false;
        }

        //处理数据包
        if (false == Udp_Common_Send_WorkThread(m_pPacketParse, m_addrRemote, m_addrLocal, tvCheck))
        {
            return false;
        }

        App_MessageBlockManager::instance()->Close(pMbData);
    }

    App_PacketParsePool::instance()->Delete(m_pPacketParse);

    m_atvInput = ACE_OS::gettimeofday();
    m_u4RecvSize += u4Len;
    m_u4RecvPacketCount++;

    return true;
}

int CReactorUDPHander::Init_Open_Address(const ACE_INET_Addr& AddrRemote)
{
    m_addrLocal = AddrRemote;

    //按照线程初始化统计模块的名字
    char szName[MAX_BUFF_50] = { '\0' };
    sprintf_safe(szName, MAX_BUFF_50, "发送线程");
    m_CommandAccount.InitName(szName, App_MainConfig::instance()->GetMaxCommandCount());

    //初始化统计模块功能
    m_CommandAccount.Init(App_MainConfig::instance()->GetCommandAccount(),
                          App_MainConfig::instance()->GetCommandFlow(),
                          App_MainConfig::instance()->GetPacketTimeOut());

    //设置发送超时时间（因为UDP如果客户端不存在的话，sendto会引起一个recv错误）
    //在这里设置一个超时，让个recv不会无限等下去
    struct timeval timeout = { MAX_RECV_UDP_TIMEOUT, 0 };
    ACE_OS::setsockopt(m_skRemote.get_handle(), SOL_SOCKET, SO_RCVTIMEO, (const char*)&timeout, sizeof(timeout));

    m_pPacketParse = App_PacketParsePool::instance()->Create(__FILE__, __LINE__);

    return 0;
}

void CReactorUDPHander::GetCommandData(uint16 u2CommandID, _CommandData& objCommandData)
{
    _CommandData* pCommandData = m_CommandAccount.GetCommandData(u2CommandID);

    if(pCommandData != NULL)
    {
        objCommandData += (*pCommandData);
    }
}

void CReactorUDPHander::GetFlowInfo(uint32& u4FlowIn, uint32& u4FlowOut)
{
    u4FlowIn  = m_CommandAccount.GetFlowIn();
    u4FlowOut = m_CommandAccount.GetFlowOut();
}
