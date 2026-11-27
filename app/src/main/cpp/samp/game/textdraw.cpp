#include "../main.h"
#include "game.h"
#include "util.h"
#include "RW/RenderWare.h"
#include "../gui/gui.h"
#include <algorithm>
#include <cmath>

extern UI* pUI;
extern CSnapShotHelper* pSnapShotHelper;

extern uintptr_t TextDrawTexture[];

// 0.3.7
CTextDraw::CTextDraw(TEXT_DRAW_TRANSMIT* pTextDrawTransmit, const char* szText)
{
    memset(&m_TextDrawData, 0, sizeof(TEXT_DRAW_DATA));

    m_TextDrawData.fLetterWidth = pTextDrawTransmit->fLetterWidth;
    m_TextDrawData.fLetterHeight = pTextDrawTransmit->fLetterHeight;

    m_TextDrawData.dwLetterColor = pTextDrawTransmit->dwLetterColor;
    m_TextDrawData.byteUnk12 = 0;
    m_TextDrawData.byteCentered = pTextDrawTransmit->byteCenter;
    m_TextDrawData.byteBox = pTextDrawTransmit->byteBox;

    m_TextDrawData.fLineWidth = pTextDrawTransmit->fLineWidth;
    m_TextDrawData.fLineHeight = pTextDrawTransmit->fLineHeight;

    m_TextDrawData.dwBoxColor = pTextDrawTransmit->dwBoxColor;
    m_TextDrawData.byteProportional = pTextDrawTransmit->byteProportional;
    m_TextDrawData.dwBackgroundColor = pTextDrawTransmit->dwBackgroundColor;
    m_TextDrawData.byteShadow = pTextDrawTransmit->byteShadow;
    m_TextDrawData.byteOutline = pTextDrawTransmit->byteOutline;
    m_TextDrawData.byteAlignLeft = pTextDrawTransmit->byteLeft;
    m_TextDrawData.byteAlignRight = pTextDrawTransmit->byteRight;
    m_TextDrawData.dwStyle = pTextDrawTransmit->byteStyle;
    // Estilo fora de 0..5 (pacote invalido/malicioso) fazia
    // CFont::SetFontStyle ler fora da tabela de fontes do GTA.
    if (m_TextDrawData.dwStyle > 5) m_TextDrawData.dwStyle = 0;

    m_TextDrawData.fX = pTextDrawTransmit->fX;
    m_TextDrawData.fY = pTextDrawTransmit->fY;

    m_TextDrawData.dwParam1 = 0xFFFFFFFF;
    m_TextDrawData.dwParam2 = 0xFFFFFFFF;
    m_TextDrawData.byteSelectable = pTextDrawTransmit->byteSelectable;
    m_TextDrawData.wModelID = pTextDrawTransmit->wModelID;
    m_TextDrawData.vecRot.x = pTextDrawTransmit->vecRot.x;
    m_TextDrawData.vecRot.y = pTextDrawTransmit->vecRot.y;
    m_TextDrawData.vecRot.z = pTextDrawTransmit->vecRot.z;
    m_TextDrawData.fZoom = pTextDrawTransmit->fZoom;
    m_TextDrawData.wColor1 = pTextDrawTransmit->wColor1;
    m_TextDrawData.wColor2 = pTextDrawTransmit->wColor2;
    m_TextDrawData.bHasKeyCode = false;
    m_TextDrawData.iTextureSlot = -1;
    m_dwNextSnapshotAttempt = 0;
    // SetText compara com o texto atual: o buffer precisa estar zerado antes.
    memset(m_szText, 0, sizeof(m_szText));
    memset(m_gxtString, 0, sizeof(m_gxtString));
    SetText(szText);

    if (m_TextDrawData.dwStyle == 4) {
        m_TextDrawData.iTextureSlot = GetFreeTextDrawTextureSlot();
        LoadTexture();
    }

    m_TextDrawData.bHasRectArea = false;
    m_rectArea.left = 0.0f;
    m_rectArea.right = 0.0f;
    m_rectArea.top = 0.0f;
    m_rectArea.bottom = 0.0f;
    m_bHovered = false;
    m_dwHoverColor = 0;

}
// 0.3.7
CTextDraw::~CTextDraw()
{
    DestroyTextDrawTexture(m_TextDrawData.iTextureSlot);
}

uintptr_t LoadFromTxdSlot(const char* szSlot, const char* szTexture)
{
    RwTexture* tex = nullptr;
    if (strncmp(szSlot, "none", 5u))
    {
        /*uintptr_t v10 = ((int (*)(const char*))(g_libGTASA + 0x55BB85))(szSlot);
        CallFunction<void>(g_libGTASA + 0x55BD6C + 1);
        CallFunction<void>(g_libGTASA + 0x55BD6C + 1, v10, 0);
        tex = CallFunction<RwTexture*>(g_libGTASA + 0x1B2558 + 1, szTexture, 0);
        CallFunction<void>(g_libGTASA + 0x55BDA8 + 1);*/
    }

    static char *texdb[8] = { "samp", "mobile", "txd", "menu", "gta3", "gta_int", "player", "playerhi" };
    bool FindedLibrary = false;
    for (int i = 0; i < 8; i++)
    {
        if (!strcmp(texdb[i], szSlot))
        {
            FindedLibrary = true;
            break;
        }
    }

    if (FindedLibrary == false)
    {
        if(!tex) tex = (RwTexture*)LoadTexture(szTexture);
    }

    if(!tex) tex = (RwTexture*)LoadTexture(std::string(std::string(szTexture) + "_" + szSlot).c_str());
    if(!tex) tex = (RwTexture*)LoadTexture(std::string(std::string(szSlot) + "_" + szTexture).c_str());
    if(!tex) tex = (RwTexture*)LoadTexture(szTexture);
    if(!tex)
    {
        std::string str = szTexture;
        std::transform(str.begin(), str.end(), str.begin(), ::tolower);
        tex = (RwTexture*)LoadTexture(str.c_str());
    }
    if(!tex)
    {
        std::string str = szTexture;
        std::transform(str.begin(), str.end(), str.begin(), ::toupper);
        tex = (RwTexture*)LoadTexture(std::string(str + "_" + szSlot).c_str());
    }

    if(!tex)
    {
        tex = (RwTexture*)CUtil::LoadTextureFromDB(szSlot, szTexture);
    }

    return (uintptr_t)tex;
}

void CTextDraw::Draw()
{
    if (m_TextDrawData.iTextureSlot != -1) {
        return DrawTextured();
    }

    if (m_TextDrawData.dwStyle == 4) {
        return DrawTextured();
    }

    if (m_TextDrawData.dwStyle == 5) {
        return;
    }

    return DrawDefault();
}

void CTextDraw::DrawDefault()
{
    if (!m_szText || !strlen(m_szText)) return;

    //CFont::AsciiToGxtChar(m_szText, m_gxtString);
    int iScreenWidth = RsGlobal->maximumWidth;
    int iScreenHeight = RsGlobal->maximumHeight;
    float fHorizHudScale = 1.0f / 640.0f;
    float fVertHudScale = 1.0f / 448.0f;

    float fScaleY = (float)iScreenHeight * fVertHudScale * m_TextDrawData.fLetterHeight * 0.5f;
    float fScaleX = (float)iScreenWidth * fHorizHudScale * m_TextDrawData.fLetterWidth;

    CFont::SetScale(fScaleX, fScaleY);

    if (m_bHovered)
    {
        uint32_t dwReversed = __builtin_bswap32(m_dwHoverColor | (0x000000FF));
        CFont::SetColor(&dwReversed);
    }
    else
    {
        CFont::SetColor(&m_TextDrawData.dwLetterColor);
    }

    CFont::SetJustify(0);

    if (m_TextDrawData.byteAlignRight) CFont::SetOrientation(2);
    else if (m_TextDrawData.byteCentered) CFont::SetOrientation(0);
    else CFont::SetOrientation(1);

    float fLineWidth = iScreenWidth * m_TextDrawData.fLineWidth * fHorizHudScale;
    CFont::SetWrapX(fLineWidth);

    float fLineHeight = iScreenWidth * m_TextDrawData.fLineHeight * fHorizHudScale;
    CFont::SetCentreSize(fLineHeight);

    CFont::SetBackground(m_TextDrawData.byteBox, 0);
    CFont::SetBackgroundColor(&m_TextDrawData.dwBoxColor);
    CFont::SetProportional(m_TextDrawData.byteProportional);
    CFont::SetDropColor(&m_TextDrawData.dwBackgroundColor);

    if (m_TextDrawData.byteOutline) {
        CFont::SetEdge(m_TextDrawData.byteOutline);
    }
    else {
        //CFont::SetEdge(m_TextDrawData.byteShadow);
        CFont::SetDropShadowPosition(m_TextDrawData.byteShadow);
    }

    CFont::SetFontStyle(m_TextDrawData.dwStyle);

    if (m_TextDrawData.bHasKeyCode) {
        //  CMessages_InsertPlayerControlKeysInString(m_szString);
    }

    float fUseY = iScreenHeight - ((448.0 - m_TextDrawData.fY) * (iScreenHeight * fVertHudScale));
    float fUseX = iScreenWidth - ((640.0 - m_TextDrawData.fX) * (iScreenWidth * fHorizHudScale));
    CFont::PrintString(fUseX, fUseY, (const char*)m_szText);

    CFont::SetEdge(0);

    if (m_TextDrawData.byteAlignRight)
    {
        m_rectArea.left = fUseX - (fLineWidth - fUseX);
        m_rectArea.right = fUseX;
        m_rectArea.bottom = fUseY;
        m_rectArea.top = fUseY + fLineHeight;
    }
    else if (m_TextDrawData.byteCentered)
    {
        m_rectArea.left = fUseX - (fLineHeight * 0.5f);
        m_rectArea.right = m_rectArea.left + fLineHeight;
        m_rectArea.bottom = fUseY;
        m_rectArea.top = fUseY + fLineWidth;
    }
    else
    {
        m_rectArea.left = fUseX;
        m_rectArea.right = fLineWidth - fUseX + fUseX;
        m_rectArea.bottom = fUseY;
        m_rectArea.top = fUseY + fLineHeight;
    }
    m_TextDrawData.bHasRectArea = true;
}

void CTextDraw::DrawTextured()
{
    float scaleX = (float)(RsGlobal->maximumWidth) * (1.0f / 640.0f);
    float scaleY = (float)(RsGlobal->maximumHeight) * (1.0f / 448.0f);

    m_rectArea.left = m_TextDrawData.fX * scaleX;
    m_rectArea.bottom = m_TextDrawData.fY * scaleY;
    m_rectArea.right = (m_TextDrawData.fX + m_TextDrawData.fLineWidth) * scaleX;
    m_rectArea.top = (m_TextDrawData.fY + m_TextDrawData.fLineHeight) * scaleY;
    m_TextDrawData.bHasRectArea = true;

    static float uv_reflected[8] = {
            0.0f, 1.0f,
            1.0f, 1.0f,
            0.0f, 0.0f,
            1.0f, 0.0f };

    static float uv_normal[8] = {
            0.0f, 0.0f,
            1.0f, 0.0f,
            0.0f, 1.0f,
            1.0f, 1.0f };

    if (m_TextDrawData.iTextureSlot < 0 || m_TextDrawData.iTextureSlot >= MAX_TEXT_DRAW_TEXTURE_SLOTS) {
        return;
    }

    uintptr_t texPtr = TextDrawTexture[m_TextDrawData.iTextureSlot];
    if (texPtr == 0) {
        // Textura inexistente no texdb (ou ainda carregando). Antes o retangulo
        // era montado e nada era desenhado; agora sai antes de mexer em estado.
        return;
    }

    // Pacote com tamanho/posicao invalidos gerava rect com NaN ou negativo e
    // sujava a tela com lixo; descarta em vez de desenhar.
    if (!std::isfinite(m_TextDrawData.fX) || !std::isfinite(m_TextDrawData.fY) ||
        !std::isfinite(m_TextDrawData.fLineWidth) || !std::isfinite(m_TextDrawData.fLineHeight) ||
        m_TextDrawData.fLineWidth <= 0.0f || m_TextDrawData.fLineHeight <= 0.0f) {
        return;
    }

    DrawTextureUV(texPtr, &m_rectArea,
                  m_bHovered ? m_dwHoverColor : m_TextDrawData.dwLetterColor,
                  m_TextDrawData.dwStyle == 5 ? uv_reflected : uv_normal);
}

void CTextDraw::SetText(const char* szText)
{
    if (szText == nullptr)
        szText = "";

    // O servidor reenvia o mesmo texto (inclusive "txd:textura" de sprite) em
    // cada atualizacao. Se nada mudou, nao recarrega textura nem aloca.
    if (strncmp(m_szText, szText, sizeof(m_szText) - 1) == 0)
        return;

    strncpy(m_szText, szText, sizeof(m_szText) - 1);
    m_szText[sizeof(m_szText) - 1] = 0;

    if (m_TextDrawData.dwStyle == 4 && m_TextDrawData.iTextureSlot != -1)
    {
        DestroyTextDrawTexture(m_TextDrawData.iTextureSlot);
        LoadTexture();
    }
}

void CTextDraw::LoadTexture()
{
    char txdname[64 + 1];
    memset(txdname, 0, sizeof(txdname));
    char texturename[64 + 1];
    memset(texturename, 0, sizeof(texturename));

    char* szTexture = strchr(m_szText, ':');
    if (szTexture == nullptr) return;

    if (strlen(m_szText) < 64 && strchr(m_szText, '\\') == nullptr && strchr(m_szText, '/') == nullptr)
    {
        strncpy(txdname, m_szText, (size_t)(szTexture - m_szText));
        strcpy(texturename, ++szTexture);

        // Cache compartilhado: mesma textura entre textdraws e falha memorizada
        // (nao repete a busca nos bancos a cada atualizacao do servidor).
        if (m_TextDrawData.iTextureSlot != -1 && txdname[0] && texturename[0])
            TextDrawTexture[m_TextDrawData.iTextureSlot] = ::GetTextDrawTextureCached(txdname, texturename);
    }
}

#include "Models/ModelInfo.h"
void CTextDraw::SnapshotProcess()
{
    extern CSnapShotHelper* pSnapShotHelper;

    if (!pSnapShotHelper) return;
    if (m_TextDrawData.dwStyle != 5 || m_TextDrawData.iTextureSlot != -1) return;

    // Cada tentativa carrega modelo e renderiza 256x256. Quando o preview nao
    // pode ser gerado, espera antes de tentar de novo (antes era todo frame).
    const uint32_t dwNow = GetTickCount();
    if (dwNow < m_dwNextSnapshotAttempt) return;
    const uint32_t dwRetryDelay = 1500;

    // Validate model ID before processing
    if (m_TextDrawData.wModelID == 0 || m_TextDrawData.wModelID > 20000)
    {
        m_dwNextSnapshotAttempt = dwNow + dwRetryDelay;
        return;
    }

    if (!std::isfinite(m_TextDrawData.fZoom) ||
        !std::isfinite(m_TextDrawData.vecRot.x) ||
        !std::isfinite(m_TextDrawData.vecRot.y) ||
        !std::isfinite(m_TextDrawData.vecRot.z))
    {
        m_dwNextSnapshotAttempt = dwNow + dwRetryDelay;
        return;
    }

    uintptr_t snapshot = 0;

    if (IsValidPedModel(m_TextDrawData.wModelID))
    {
        snapshot = (uintptr_t)pSnapShotHelper->CreatePedSnapShot(
                m_TextDrawData.wModelID,
                m_TextDrawData.dwBackgroundColor,
                &m_TextDrawData.vecRot,
                m_TextDrawData.fZoom);
    }
    else if (m_TextDrawData.wModelID >= 400 && m_TextDrawData.wModelID <= 611)
    {
        snapshot = (uintptr_t)pSnapShotHelper->CreateVehicleSnapShot(
                m_TextDrawData.wModelID,
                m_TextDrawData.dwBackgroundColor,
                &m_TextDrawData.vecRot,
                m_TextDrawData.fZoom,
                m_TextDrawData.wColor1,
                m_TextDrawData.wColor2);
    }
    else
    {
        if (!CModelInfo::GetModelInfo(m_TextDrawData.wModelID)) {
            Log("Invalid model ID %d for snapshot", m_TextDrawData.wModelID);
            m_dwNextSnapshotAttempt = dwNow + dwRetryDelay;
            return;
        }
        snapshot = (uintptr_t)pSnapShotHelper->CreateObjectSnapShot(
                m_TextDrawData.wModelID,
                m_TextDrawData.dwBackgroundColor,
                &m_TextDrawData.vecRot,
                m_TextDrawData.fZoom);
    }

    if (snapshot && snapshot != 0xFFFFFFFF && snapshot != 0) {
        m_TextDrawData.iTextureSlot = GetFreeTextDrawTextureSlot();
        if (m_TextDrawData.iTextureSlot != -1) {
            TextDrawTexture[m_TextDrawData.iTextureSlot] = snapshot;
            return;
        }
        // Sem slot livre: descarta o raster para nao vazar memoria.
        RwTextureDestroy(reinterpret_cast<RwTexture*>(snapshot));
    }

    m_dwNextSnapshotAttempt = dwNow + dwRetryDelay;
}