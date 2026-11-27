#pragma once

#define INVALID_OBJECT_ID	0xFFFF

class CObjectPool
{
public:
    CObjectPool();
    ~CObjectPool();

    bool New(OBJECTID ObjectID, int iModel, CVector vecPos, CVector vecRot, float fDrawDistance);
    bool Delete(OBJECTID ObjectID);

    void Process();

    // 0.3.7
    CObject* GetAt(OBJECTID ObjectID) {
        // ObjectID == MAX_OBJECTS estourava os dois arrays (1000 posicoes).
        if (ObjectID < MAX_OBJECTS && m_bObjectSlotState[ObjectID])
            return m_pObjects[ObjectID];

        return nullptr;
    }

    CObject *GetObjectFromGtaPtr(CEntityGTA *pGtaObject)
    {
        // Comecava em 1 e parava antes de MAX: o objeto do slot 0 e o do
        // ultimo slot nunca eram encontrados.
        for (OBJECTID x = 0; x < MAX_OBJECTS; ++x)
        {
            if (m_pObjects[x] && pGtaObject == m_pObjects[x]->m_pEntity)
                return m_pObjects[x];
        }

        return nullptr;
    }

    OBJECTID FindIDFromGtaPtr(CPhysical* pGtaObject);

    CObject* FindObjectFromGtaPtr(CPhysical* pGtaObject);

    void ProcessMaterialText();
    int GetCount() { return m_iObjectCount; }

private:
    int			m_iObjectCount;
    bool		m_bObjectSlotState[MAX_OBJECTS];
    CObject*	m_pObjects[MAX_OBJECTS];
};