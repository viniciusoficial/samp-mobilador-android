#include "../main.h"
#include "game.h"
#include "../net/netgame.h"

#include "RW/RenderWare.h"
#include "Streaming.h"
#include "game/Models/ModelInfo.h"
#include "util.h"
#include "util/CUtil.h"

#include <cmath>

extern CGame* pGame;
extern CNetGame* pNetGame;
extern MaterialTextGenerator* pMaterialTextGenerator;

CObject::CObject(int iModel, CVector vecPos, CVector vecRot, float fDrawDistance, uint8_t bAttached)
{
    // O construtor era vazio: m_pEntity/m_dwGTAId e todos os materiais ficavam
    // com lixo da heap. Nenhum CreateObject do servidor criava nada e qualquer
    // SetMaterial podia chamar RwTextureDestroy em ponteiro aleatorio.
    m_pEntity = nullptr;
    m_dwGTAId = 0;
    m_iModel = iModel;
    m_byteMoving = 0;
    m_fMoveSpeed = 0.0f;
    m_bNeedRotate = false;
    m_bForceRender = false;
    m_bAttachedToPed = false;
    m_bSyncRotation = false;
    m_AttachedObjectID = 0xFFFF;
    m_AttachedVehicleID = 0xFFFF;
    m_MaterialTextIndex = 0;
    m_bHasMaterial = false;
    m_bHasMaterialText = false;
    m_vecAttachedPos = CVector{ 0.0f, 0.0f, 0.0f };
    m_vecAttachedRot = CVector{ 0.0f, 0.0f, 0.0f };
    m_vecRotation = vecRot;
    m_vecRotationTarget = vecRot;
    m_vecRotationStart = vecRot;
    memset(&m_matTarget, 0, sizeof(m_matTarget));
    m_matTarget.right.x = 1.0f;
    m_matTarget.up.y = 1.0f;
    m_matTarget.at.z = 1.0f;
    m_matTarget.pos.x = vecPos.x;
    m_matTarget.pos.y = vecPos.y;
    m_matTarget.pos.z = vecPos.z;
    m_fDistanceToTargetPoint = 0.0f;
    m_dwMoveTick = 0;

    for (int i = 0; i < 16; ++i)
    {
        m_MaterialTexture[i] = 0;
        m_MaterialTextTexture[i] = 0;
        m_dwMaterialColor[i] = 0;
        m_iMaterialType[i] = 0;
        m_szMaterialText[i] = nullptr;
        m_iMaterialSize[i] = 0;
        m_iMaterialFontSize[i] = 0;
        m_dwMaterialFontColor[i] = 0;
        m_dwMaterialBackColor[i] = 0;
        m_iMaterialTextAlign[i] = 0;
    }

    (void)fDrawDistance;
    (void)bAttached;

    if (iModel <= 0 || iModel >= 20000)
    {
        LOGW("objeto: modelo invalido %d", iModel);
        return;
    }

    // O opcode create_object (0x0107) chama
    // CEntity::GetDistanceFromCentreOfMassToBaseOfModel para corrigir a altura
    // do objeto usando a caixa de colisao do modelo. Sem o modelo carregado
    // antes (CModelInfo/col nulos) ele estourava em libGame +0x8 -> SIGSEGV em
    // CRunningScript::ProcessCommands200To299. Veiculos e peds daqui ja fazem
    // exatamente isso: carregar o modelo e so depois usar o opcode.
    if (CModelInfo::GetModelInfo(iModel) == nullptr)
    {
        LOGW("objeto: modelo %d nao existe", iModel);
        return;
    }

    if (!CStreaming::TryLoadModel(iModel))
    {
        LOGW("objeto: modelo %d nao carregou", iModel);
        return;
    }

    // Cria o objeto pelo proprio opcode do jogo (0x0107), igual ao que o
    // playerped ja faz para o copo/cigarro com o modelo pre-carregado.
    uintptr handle = 0;
    ScriptCommand(&create_object, iModel, vecPos.x, vecPos.y, vecPos.z, &handle);

    // Em falha o opcode devolve handle -1 (ou um negativo); handle >= 3000
    // nunca e' valido (3000 e o tamanho do object pool em CPools::ms_pObjectPool).
    if (handle == 0 || handle >= 3000)
    {
        LOGW("objeto: create_object falhou model=%d handle=%d", iModel, (int)handle);
        CStreaming::RemoveModelIfNoRefs(iModel);
        return;
    }

    m_dwGTAId = static_cast<uint32_t>(handle);
    m_pEntity = GamePool_Object_GetAt(static_cast<int>(m_dwGTAId));

    if (m_pEntity == nullptr)
    {
        LOGW("objeto: pool sem objeto model=%d handle=%d", iModel, (int)m_dwGTAId);
        m_dwGTAId = 0;
        CStreaming::RemoveModelIfNoRefs(iModel);
        return;
    }

    // Um handle reaproveitado nao pode virar "objeto criado" com outro modelo,
    // e nesse caso o objeto do jogo nao e' nosso: nao destroi nada.
    if (m_pEntity->m_nModelIndex != static_cast<uint16_t>(iModel))
    {
        LOGW("objeto: handle %d tem modelo %d (pedido %d)", (int)m_dwGTAId, (int)m_pEntity->m_nModelIndex, iModel);
        m_pEntity = nullptr;
        m_dwGTAId = 0;
        CStreaming::RemoveModelIfNoRefs(iModel);
        return;
    }

    if (vecRot.x != 0.0f || vecRot.y != 0.0f || vecRot.z != 0.0f)
        InstantRotate(vecRot.x, vecRot.y, vecRot.z);

    m_vecRotation = vecRot;
}
CObject::~CObject()
{
    m_iModel = 0;

    for (int i = 0; i < 16; ++i)
    {
        // m_MaterialTexture vem do banco de texturas compartilhado: destruir
        // aqui quebrava a textura de outros objetos que usam a mesma txd.
        m_MaterialTexture[i] = 0;

        if (m_MaterialTextTexture[i])
        {
            RwTextureDestroy(reinterpret_cast<RwTexture *>(m_MaterialTextTexture[i]));
            m_MaterialTextTexture[i] = 0;
        }

        if (m_szMaterialText[i] != nullptr)
        {
            delete[] m_szMaterialText[i];
            m_szMaterialText[i] = nullptr;
        }
    }

    // m_dwGTAId 0 e um handle valido (slot 0 do pool), por isso a validacao e
    // pelo pool, e confere o modelo para nao destruir um objeto que herdou o slot.
    if (g_libGTASA && m_pEntity != nullptr && m_dwGTAId != 0)
    {
        CPhysical* pEntity = GamePool_Object_GetAt(static_cast<int>(m_dwGTAId));
        if (pEntity != nullptr && pEntity == m_pEntity)
        {
            ScriptCommand(&destroy_object, static_cast<int>(m_dwGTAId));
        }

        CStreaming::RemoveModelIfNoRefs(m_iModel);
    }

    m_pEntity = nullptr;
    m_dwGTAId = 0;
}
void CObject::Process(float fElapsedTime)
{
	// Sem entidade o objeto nao existe no mundo (modelo ausente no aparelho):
	// nao ha nada para mover/anexar e mexer em m_pEntity nulo estourava.
	if (m_pEntity == nullptr || m_dwGTAId == 0)
		return;

	// O objeto deixou de existir no pool do jogo (modelo removido, por exemplo):
	// naoprocessa nem tenta anexos, senao entrega m_dwGTAId inválido ao script.
	if (GamePool_Object_GetAt(static_cast<int>(m_dwGTAId)) == nullptr)
	{
		LOGW("objeto %d: handle %d nao existe mais no pool", m_iModel, (int)m_dwGTAId);
		m_pEntity = nullptr;
		return;
	}

	// Anexado a um veiculo: quem move e' o veiculo. Anexa uma unica vez, quando
	// o veiculo ja existe no mundo (o CreateObject pode chegar antes dele).
	if (m_AttachedVehicleID != INVALID_VEHICLE_ID)
	{
		if (pNetGame && !ScriptCommand(&is_object_attached, m_dwGTAId))
		{
			CVehiclePool* pVehiclePool = pNetGame->GetVehiclePool();
			CVehicle* pVehicle = pVehiclePool ? pVehiclePool->GetAt(m_AttachedVehicleID) : nullptr;

			if (pVehicle && pVehicle->m_pVehicle && pVehicle->m_pVehicle->IsAdded())
				AttachToVehicle(pVehicle);
		}

		return;
	}

	// Anexado a outro objeto: segue a matriz do objeto pai.
	if (m_AttachedObjectID != INVALID_OBJECT_ID)
	{
		if (pNetGame && !ScriptCommand(&is_object_attached, m_dwGTAId))
		{
			CObjectPool* pObjectPool = pNetGame->GetObjectPool();
			CObject* pObject = pObjectPool ? pObjectPool->GetAt(m_AttachedObjectID) : nullptr;

			if (pObject && pObject->m_pEntity && pObject->m_dwGTAId != m_dwGTAId)
				AttachToObject(pObject);
		}

		return;
	}

	if (!(m_byteMoving & 1))
		return;

	// dt do primeiro frame ou apos travada pode vir 0 ou enorme; limitar evita
	// divisao por zero e salto de centenas de metros de uma vez.
	if (fElapsedTime <= 0.0f)
		fElapsedTime = 0.016f;
	if (fElapsedTime > 0.25f)
		fElapsedTime = 0.25f;

	if (GamePool_Object_GetAt(static_cast<int>(m_dwGTAId)) == nullptr)
		return;

	RwMatrix mat = m_pEntity->GetMatrix().ToRwMatrix();
	float remaining = DistanceRemaining(&mat);
	float step = m_fMoveSpeed * fElapsedTime;

	// Chegou (ou o servidor mandou velocidade 0): encaixa no destino e para.
	// Sem esta saida um MoveObject com speed 0 ficava preso em m_byteMoving.
	if (m_fMoveSpeed <= 0.0001f || remaining <= 0.01f || step >= remaining)
	{
		SetPos(m_matTarget.pos.x, m_matTarget.pos.y, m_matTarget.pos.z);

		if (m_bNeedRotate)
			SetRotation(&m_vecRotationTarget);

		StopMoving();
		return;
	}

	// Fracao do caminho restante coberta neste frame.
	float fProgress = step / remaining;
	if (fProgress < 0.0f) fProgress = 0.0f;
	if (fProgress > 1.0f) fProgress = 1.0f;

	mat.pos.x += (m_matTarget.pos.x - mat.pos.x) * fProgress;
	mat.pos.y += (m_matTarget.pos.y - mat.pos.y) * fProgress;
	mat.pos.z += (m_matTarget.pos.z - mat.pos.z) * fProgress;

	// Aplica com o opcode do proprio jogo (put_object_at). A versao antiga
	// mexia em SetVelocity/SetMatrix e chamava um slot de vtable por offset
	// numerico (CPhysical::Remove): o objeto saia do mundo por um frame e o
	// slot podia nao ser o esperado no build 64 bits.
	ScriptCommand(&put_object_at, m_dwGTAId, mat.pos.x, mat.pos.y, mat.pos.z);

	if (m_bNeedRotate)
	{
		float fRotProgress = 1.0f;

		if (m_fDistanceToTargetPoint > 0.01f)
		{
			fRotProgress = 1.0f - (remaining / m_fDistanceToTargetPoint);

			if (fRotProgress < 0.0f) fRotProgress = 0.0f;
			if (fRotProgress > 1.0f) fRotProgress = 1.0f;
		}

		CVector vecRot;
		vecRot.x = m_vecRotationStart.x + subAngle(m_vecRotationStart.x, m_vecRotationTarget.x) * fRotProgress;
		vecRot.y = m_vecRotationStart.y + subAngle(m_vecRotationStart.y, m_vecRotationTarget.y) * fRotProgress;
		vecRot.z = m_vecRotationStart.z + subAngle(m_vecRotationStart.z, m_vecRotationTarget.z) * fRotProgress;

		SetRotation(&vecRot);
	}
}

// 0.3.7
void CObject::SetRotation(CVector * vecRotation)
{
	if (m_pEntity && GamePool_Object_GetAt(m_dwGTAId))
	{
		ScriptCommand(&set_object_rotation, m_dwGTAId, vecRotation->x, vecRotation->y, vecRotation->z);
		m_vecRotation.x = vecRotation->x;
		m_vecRotation.y = vecRotation->y;
		m_vecRotation.z = vecRotation->z;
	}
}

// Converts degrees to radians
// keywords: 0.017453292 flt_8595EC
constexpr float DegreesToRadians(float angleInDegrees) {
    return angleInDegrees * PI / 180.0F;
}

void CObject::InstantRotate(float x, float y, float z)
{
    if (!m_pEntity || m_dwGTAId == 0) return;

    x = DegreesToRadians(x);
    y = DegreesToRadians(y);
    z = DegreesToRadians(z);

    // CPhysical::Remove
    ((void (*)(CEntityGTA*))(*(uintptr_t*)( *(uintptr*)(m_pEntity) + (VER_x32 ? 0x10 : 0x10*2) )))(m_pEntity);

    m_pEntity->SetOrientation(x, y, z);

    m_pEntity->UpdateRW();
    m_pEntity->UpdateRwFrame();

    m_pEntity->Add();
}

// 0.3.7
void CObject::GetRotation(float* pfX, float* pfY, float* pfZ)
{
    if (!m_pEntity) return;

    m_pEntity->m_matrix->ConvertToEulerAngles(pfX, pfY, pfZ, 21);

    *pfX = *pfX * 57.295776 * -1.0;
    *pfY = *pfY * 57.295776 * -1.0;
    *pfZ = *pfZ * 57.295776 * -1.0;
}
// 0.3.7
void CObject::RotateMatrix(CVector vecRot)
{
	m_vecRotation = vecRot;

	vecRot.x *= 0.017453292f; // x * pi/180
	vecRot.y *= 0.017453292f; // y * pi/180
	vecRot.z *= 0.017453292f; // z * pi/180

	float cosx = cos(vecRot.x);
	float sinx = sin(vecRot.x);

	float cosy = cos(vecRot.y);
	float siny = sin(vecRot.y);

	float cosz = cos(vecRot.z);
	float sinz = sin(vecRot.z);

	float sinzx = sinz * sinx;
	float coszx = cosz * sinx;

	m_matTarget.right.x = cosz * cosy - sinzx * siny;
	m_matTarget.right.y = coszx * siny + sinz * cosy;
	m_matTarget.right.z = -(siny * cosx);
	m_matTarget.up.x = -(sinz * cosx);
	m_matTarget.up.y = cosz * cosx;
	m_matTarget.up.z = sinx;
	m_matTarget.at.x = sinzx * cosy + cosz * siny;
	m_matTarget.at.y = sinz * siny - coszx * cosy;
	m_matTarget.at.z = cosy * cosx;
}
// 0.3.7
void CObject::ApplyMoveSpeed()
{
	if (m_pEntity)
	{
		float fTimeStep = *(float*)(g_libGTASA + 0xA1A364);

		RwMatrix mat;
        mat = m_pEntity->GetMatrix().ToRwMatrix();
		mat.pos.x += fTimeStep * m_pEntity->GetMoveSpeed().x;
		mat.pos.y += fTimeStep * m_pEntity->GetMoveSpeed().y;
		mat.pos.z += fTimeStep * m_pEntity->GetMoveSpeed().z;
		m_pEntity->SetMatrix((CMatrix&)mat);
	}
}
// 0.3.7
float CObject::DistanceRemaining(RwMatrix* matPos)
{
	float	fSX, fSY, fSZ;
	fSX = (matPos->pos.x - m_matTarget.pos.x) * (matPos->pos.x - m_matTarget.pos.x);
	fSY = (matPos->pos.y - m_matTarget.pos.y) * (matPos->pos.y - m_matTarget.pos.y);
	fSZ = (matPos->pos.z - m_matTarget.pos.z) * (matPos->pos.z - m_matTarget.pos.z);
	return (float)sqrt(fSX + fSY + fSZ);
}

void CObject::SetMaterial(int iModel, int iMaterialIndex, char* txdname, char* texturename, uint32_t dwColor)
{
	// Indice negativo ou acima de 15 escrevia antes dos arrays de 16 posicoes.
	if (iMaterialIndex < 0 || iMaterialIndex >= 16 || texturename == nullptr)
		return;

	// O material do SA-MP vem com txd + textura; a versao antiga ignorava o
	// txdname e procurava a textura em qualquer banco, o que trocava ou perdia
	// a textura de placas/objetos. Procura primeiro no txd informado.
	RwTexture* texture = nullptr;
	if (txdname != nullptr && txdname[0] != '\0' && strncmp(txdname, "none", 4) != 0)
		texture = CUtil::LoadTextureFromDB(txdname, texturename);

	if (texture == nullptr)
		texture = reinterpret_cast<RwTexture *>(LoadTexture(texturename));

	// Nao destrói a textura anterior: ela pertence ao txd do jogo e pode estar
	// sendo usada por outros objetos.
	m_MaterialTexture[iMaterialIndex] = reinterpret_cast<uintptr_t>(texture);
	m_dwMaterialColor[iMaterialIndex] = dwColor;
	m_iMaterialType[iMaterialIndex] = MATERIAL_TYPE_MATERIAL;
	m_bHasMaterial = true;

	if (texture == nullptr)
		LOGW("objeto %d: textura %s/%s nao encontrada", iModel, txdname ? txdname : "none", texturename);
}

void CObject::SetMaterialText(int index, char* text, int materialSize, char* fontname, int fontSize, bool bold,
	uint32_t dwFontColor, uint32_t dwBackColor, int textAlignment)
{
	// index vinha do RPC sem limite inferior e 'text' sem validacao: indice
	// negativo escrevia antes do array e o strcpy estourava o buffer de 2048.
	if (index < 0 || index >= 16 || text == nullptr) return;

	if (m_MaterialTextTexture[index]) {
        RwTextureDestroy(reinterpret_cast<RwTexture *>(m_MaterialTextTexture[index]));
		m_MaterialTextTexture[index] = 0;
	}

    m_MaterialTextIndex = index;

	m_dwMaterialColor[index] = 0;
	m_iMaterialType[index] = MATERIAL_TYPE_TEXT;

	if (m_szMaterialText[index] != nullptr) {
		delete[] m_szMaterialText[index];
		m_szMaterialText[index] = nullptr;
	}

	m_szMaterialText[index] = new (std::nothrow) char[2048];
	if (m_szMaterialText[index] == nullptr) {
		LOGW("objeto %d: falha de memoria no textdraw %d", m_iModel, index);
		return;
	}
	strncpy(m_szMaterialText[index], text, 2047);
	m_szMaterialText[index][2047] = '\0';

	m_iMaterialSize[index] = materialSize;
	m_iMaterialFontSize[index] = fontSize;
	m_dwMaterialFontColor[index] = dwFontColor;
	m_dwMaterialBackColor[index] = dwBackColor;
	m_iMaterialTextAlign[index] = textAlignment;
}

void CObject::ProcessMaterialText()
{
	for (int i = 0; i < 16; i++)
	{
		if (pMaterialTextGenerator == nullptr)
			return;

		if (m_iMaterialType[i] == MATERIAL_TYPE_TEXT && m_MaterialTextTexture[i] == 0 &&
			m_szMaterialText[i] != nullptr && m_szMaterialText[i][0] != '\0')
		{
			// Antes o tamanho da fonte era reduzido no proprio membro a cada
			// tentativa; se Generate() falhasse, o texto encolhia 0.75x por
			// frame ate zerar e recriava rasters sem parar.
			const int scaledFontSize = static_cast<int>(m_iMaterialFontSize[i] * 0.75f);
			m_MaterialTextTexture[i] = reinterpret_cast<uintptr_t>(pMaterialTextGenerator->Generate(
                    m_szMaterialText[i], m_iMaterialSize[i], scaledFontSize,
                    false, m_dwMaterialFontColor[i], m_dwMaterialBackColor[i],
                    m_iMaterialTextAlign[i]));
			m_bHasMaterialText = true;
		}
	}
}

// 0.3.7
void CObject::MoveTo(float fX, float fY, float fZ, float fSpeed, float fRotX, float fRotY, float fRotZ)
{
	// Objeto sem entidade (modelo ausente) nao pode ser movido: antes disto o
	// MoveObject do servidor estourava em m_pEntity nulo.
	if (!m_pEntity)
		return;

	RwMatrix mat;
    mat = m_pEntity->GetMatrix().ToRwMatrix();

	if (m_byteMoving & 1) {
		this->StopMoving();
		mat.pos.x = m_matTarget.pos.x;
		mat.pos.y = m_matTarget.pos.y;
		mat.pos.z = m_matTarget.pos.z;

		if (m_bNeedRotate) {
			m_quatTarget.GetMatrix(reinterpret_cast<RwMatrix *>(&mat));
		}

        // CPhysical::Remove
        ((void (*)(CEntityGTA*))(*(uintptr_t*)( *(uintptr*)(m_pEntity) + (VER_x32 ? 0x10 : 0x10*2) )))(m_pEntity);

		m_pEntity->SetMatrix((CMatrix&)mat);
        m_pEntity->UpdateRW();
        m_pEntity->UpdateRwFrame();

        m_pEntity->Add();
	}

	m_dwMoveTick = GetTickCount();
	m_fMoveSpeed = fSpeed;
	m_matTarget.pos.x = fX;
	m_matTarget.pos.y = fY;
	m_matTarget.pos.z = fZ;
	m_byteMoving |= 1;

	if (fRotX <= -999.0f || fRotY <= -999.0f || fRotZ <= -999.0f) {
		m_bNeedRotate = false;
	}
	else
	{
		m_bNeedRotate = true;

		CVector vecRot;
		RwMatrix matrix;
		this->GetRotation(&vecRot.x, &vecRot.y, &vecRot.z);
		m_vecRotationTarget.x = fixAngle(fRotX);
		m_vecRotationTarget.y = fixAngle(fRotY);
		m_vecRotationTarget.z = fixAngle(fRotZ);

		m_vecSubRotationTarget.x = subAngle(vecRot.x, fRotX);
		m_vecSubRotationTarget.y = subAngle(vecRot.y, fRotY);
		m_vecSubRotationTarget.z = subAngle(vecRot.z, fRotZ);

		// Guarda de onde a rotacao parte: Process interpola dela ate
		// m_vecRotationTarget em graus (mesma convencao do set_object_rotation).
		m_vecRotationStart = vecRot;

		this->RotateMatrix(CVector{ fRotX, fRotY, fRotZ });
        matrix = m_pEntity->GetMatrix().ToRwMatrix();
		m_quatStart.SetFromMatrix(&matrix);
		m_quatTarget.SetFromMatrix(&m_matTarget);
		m_quatStart.Normalize();
		m_quatTarget.Normalize();
	}

	m_fDistanceToTargetPoint = m_pEntity->GetDistanceFromPoint(m_matTarget.pos.x, m_matTarget.pos.y,
                                                               m_matTarget.pos.z);

	if (pNetGame) {
		CPlayerPool* pPlayerPool = pNetGame->GetPlayerPool();
		if (pPlayerPool) {
			//pPlayerPool->GetLocalPlayer()->UpdateSurfing();
		}
	}

	// sub_1009F070
}
// 0.3.7
void CObject::StopMoving()
{
	if (!m_pEntity)
		return;

	CVector vec = { 0.0f, 0.0f, 0.0f };
	m_pEntity->SetVelocity(vec);
	m_pEntity->SetTurnSpeed(vec);

	m_byteMoving &= ~1;
}

// 0.3.7
void CObject::SetAttachedObject(uint16_t ObjectID, CVector* vecPos, CVector* vecRot, bool bSyncRotation)
{
	// Era vazio: CreateObject com objeto anexado criava a entidade no chao e
	// nunca a prendia em nada. Guarda o alvo e anexa quando ele existir.
	m_AttachedObjectID = ObjectID;
	m_AttachedVehicleID = INVALID_VEHICLE_ID;
	m_bSyncRotation = bSyncRotation;
	m_bAttachedToPed = false;

	if (vecPos) m_vecAttachedPos = *vecPos;
	if (vecRot) m_vecAttachedRot = *vecRot;

	// Anexado nao anda sozinho: um MoveObject pendente brigaria com a matriz do pai.
	m_byteMoving &= ~1;

	if (!pNetGame || m_pEntity == nullptr || m_dwGTAId == 0)
		return;

	CObjectPool* pObjectPool = pNetGame->GetObjectPool();
	CObject* pObject = pObjectPool ? pObjectPool->GetAt(m_AttachedObjectID) : nullptr;

	if (pObject && pObject->m_pEntity && pObject->m_dwGTAId != m_dwGTAId)
		AttachToObject(pObject);
}
// 0.3.7
void CObject::SetAttachedVehicle(uint16_t VehicleID, CVector* vecPos, CVector* vecRot)
{
	m_AttachedVehicleID = VehicleID;
	m_AttachedObjectID = INVALID_OBJECT_ID;
	m_bAttachedToPed = false;

	if (vecPos) m_vecAttachedPos = *vecPos;
	if (vecRot) m_vecAttachedRot = *vecRot;

	m_byteMoving &= ~1;

	if (!pNetGame || m_pEntity == nullptr || m_dwGTAId == 0)
		return;

	CVehiclePool* pVehiclePool = pNetGame->GetVehiclePool();
	CVehicle* pVehicle = pVehiclePool ? pVehiclePool->GetAt(m_AttachedVehicleID) : nullptr;

	if (pVehicle && pVehicle->m_pVehicle && pVehicle->m_pVehicle->IsAdded())
		AttachToVehicle(pVehicle);
}
// 0.3.7
void CObject::AttachToVehicle(CVehicle* pVehicle)
{
	// attach_object_to_car usa o handle do pool (m_dwGTAId), o mesmo tipo de
	// handle de objeto/veiculo que os outros opcodes de script usam.
	if (!pVehicle || pVehicle->m_pVehicle == nullptr || m_pEntity == nullptr || m_dwGTAId == 0)
		return;

	if (GamePool_Object_GetAt(static_cast<int>(m_dwGTAId)) == nullptr)
		return;

	// Reanexar a cada frame fazia o objeto piscar/saltar no veiculo.
	if (ScriptCommand(&is_object_attached, m_dwGTAId))
		return;

	ScriptCommand(&attach_object_to_car,
	              static_cast<int>(m_dwGTAId),
	              static_cast<int>(pVehicle->m_dwGTAId),
	              m_vecAttachedPos.x, m_vecAttachedPos.y, m_vecAttachedPos.z,
	              m_vecAttachedRot.x, m_vecAttachedRot.y, m_vecAttachedRot.z);
}
void CObject::AttachToObject(CObject* pObject)
{
    if (m_dwGTAId == 0) return;
    if (GamePool_Object_GetAt(static_cast<int>(m_dwGTAId))) {
        if (!ScriptCommand(&is_object_attached, m_dwGTAId)) {
            ScriptCommand(&attach_object_to_object,
                          static_cast<int>(m_dwGTAId),
                          static_cast<int>(pObject->m_dwGTAId),
                          m_vecAttachedPos.x,
                          m_vecAttachedPos.y,
                          m_vecAttachedPos.z,
                          m_vecAttachedRot.x,
                          m_vecAttachedRot.y,
                          m_vecAttachedRot.z);
        }
    }
} 

bool CObject::AttachedToMovingEntity()
{
	// Usado pelo render/hook: objeto preso a veiculo ou a outro objeto precisa
	// ser redesenhado longe do centro do mapa.
	return (m_AttachedVehicleID != INVALID_VEHICLE_ID || m_AttachedObjectID != INVALID_OBJECT_ID);
}

void CObject::SetPos(float x, float y, float z)
{
    if (m_dwGTAId == 0 || GamePool_Object_GetAt(static_cast<int>(m_dwGTAId)) == nullptr)
        return;

    ScriptCommand(&put_object_at, static_cast<int>(m_dwGTAId), x, y, z);
}