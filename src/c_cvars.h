#ifndef __C_CVARS_H__
#define __C_CVARS_H__

#include "doomtype.h"
#include "tarray.h"

/*
==========================================================

CVARS (console variables)

==========================================================
*/

enum
{
	CVAR_ARCHIVE		= 1,	// set to cause it to be saved to config
	CVAR_USERINFO		= 2,	// added to userinfo  when changed
	CVAR_SERVERINFO		= 4,	// added to serverinfo when changed
	CVAR_NOSET			= 8,	// don't allow change from console at all,
								// but can be set from the command line
	CVAR_LATCH			= 16,	// save changes until server restart
	CVAR_UNSETTABLE		= 32,	// can unset this var from console
	CVAR_DEMOSAVE		= 64,	// save the value of this cvar in a demo
	CVAR_ISDEFAULT		= 128,	// is cvar unchanged since creation?
	CVAR_AUTO			= 256,	// allocated; needs to be freed when destroyed
	CVAR_NOINITCALL		= 512,	// don't call callback at game start
	CVAR_GLOBALCONFIG	= 1024,	// cvar is saved to global config section
	CVAR_VIDEOCONFIG	= 2048, // cvar is saved to video config section
};

union UCVarValue
{
	bool Bool;
	int Int;
	float Float;
	char *String;
};

enum ECVarType
{
	CVAR_Bool,
	CVAR_Int,
	CVAR_Float,
	CVAR_String,
	CVAR_Color,		// stored as CVAR_Int
	CVAR_Dummy		// just redirects to another cvar
};

class FConfigFile;
class AActor;

class FBaseCVar
{
public:
	FBaseCVar (const char *name, DWORD flags, void (*callback)(FBaseCVar &));
	virtual ~FBaseCVar ();

	inline void Callback () { if (m_Callback) m_Callback (*this); }

	inline const char *GetName () const { return Name; }
	inline DWORD GetFlags () const { return Flags; }

	void CmdSet (const char *newval);
	void ForceSet (UCVarValue value, ECVarType type);
	void SetGenericRep (UCVarValue value, ECVarType type);
	void ResetToDefault ();

	virtual ECVarType GetRealType () const = 0;

	virtual UCVarValue GetGenericRep (ECVarType type) const = 0;
	virtual UCVarValue GetFavoriteRep (ECVarType *type) const = 0;

	virtual UCVarValue GetGenericRepDefault (ECVarType type) const = 0;
	virtual UCVarValue GetFavoriteRepDefault (ECVarType *type) const = 0;
	virtual void SetGenericRepDefault (UCVarValue value, ECVarType type) = 0;

	FBaseCVar &operator= (const FBaseCVar &var)
		{ UCVarValue val; ECVarType type; val = var.GetFavoriteRep (&type); SetGenericRep (val, type); return *this; }

	static void EnableNoSet ();		// enable the honoring of CVAR_NOSET
	static void EnableCallbacks ();
	static void ResetColors ();		// recalc color cvars' indices after screen change

	static void ListVars (const char *filter);

protected:
	FBaseCVar () {}
	virtual void DoSet (UCVarValue value, ECVarType type) = 0;

	static bool ToBool (UCVarValue value, ECVarType type);
	static int ToInt (UCVarValue value, ECVarType type);
	static float ToFloat (UCVarValue value, ECVarType type);
	static char *ToString (UCVarValue value, ECVarType type);
	static UCVarValue FromBool (bool value, ECVarType type);
	static UCVarValue FromInt (int value, ECVarType type);
	static UCVarValue FromFloat (float value, ECVarType type);
	static UCVarValue FromString (const char *value, ECVarType type);

	char *Name;
	DWORD Flags;

private:
	FBaseCVar (const FBaseCVar &var);
	FBaseCVar (const char *name, DWORD flags);

	void (*m_Callback)(FBaseCVar &);
	FBaseCVar *m_Next;

	static bool m_UseCallback;
	static bool m_DoNoSet;

	// Writes all cvars that could effect demo sync to *demo_p. These are
	// cvars that have either CVAR_SERVERINFO or CVAR_DEMOSAVE set.
	friend void C_WriteCVars (byte **demo_p, DWORD filter, bool compact=false);

	// Read all cvars from *demo_p and set them appropriately.
	friend void C_ReadCVars (byte **demo_p);

	// Backup demo cvars. Called before a demo starts playing to save all
	// cvars the demo might change.
	friend void C_BackupCVars (void);

	// Finds a named cvar
	friend FBaseCVar *FindCVar (const char *var_name, FBaseCVar **prev);
	friend FBaseCVar *FindCVarSub (const char *var_name, int namelen);

	// Called from G_InitNew()
	friend void UnlatchCVars (void);

	// archive cvars to FILE f
	friend void C_ArchiveCVars (FConfigFile *f, int type);

	// initialize cvars to default values after they are created
	friend void C_SetCVarsToDefaults (void);

	friend void FilterCompactCVars (TArray<FBaseCVar *> &cvars, DWORD filter);
};

class FBoolCVar : public FBaseCVar
{
public:
	FBoolCVar (const char *name, bool def, DWORD flags, void (*callback)(FBoolCVar &)=NULL);

	virtual ECVarType GetRealType () const;

	virtual UCVarValue GetGenericRep (ECVarType type) const;
	virtual UCVarValue GetFavoriteRep (ECVarType *type) const;
	virtual UCVarValue GetGenericRepDefault (ECVarType type) const;
	virtual UCVarValue GetFavoriteRepDefault (ECVarType *type) const;
	virtual void SetGenericRepDefault (UCVarValue value, ECVarType type);

	inline bool operator= (bool boolval)
		{ UCVarValue val; val.Bool = boolval; SetGenericRep (val, CVAR_Bool); return boolval; }
	inline operator bool () const { return Value; }
	inline bool operator *() const { return Value; }

protected:
	virtual void DoSet (UCVarValue value, ECVarType type);

	bool Value;
	bool DefaultValue;
};

class FIntCVar : public FBaseCVar
{
public:
	FIntCVar (const char *name, int def, DWORD flags, void (*callback)(FIntCVar &)=NULL);

	virtual ECVarType GetRealType () const;

	virtual UCVarValue GetGenericRep (ECVarType type) const;
	virtual UCVarValue GetFavoriteRep (ECVarType *type) const;
	virtual UCVarValue GetGenericRepDefault (ECVarType type) const;
	virtual UCVarValue GetFavoriteRepDefault (ECVarType *type) const;
	virtual void SetGenericRepDefault (UCVarValue value, ECVarType type);

	int operator= (int intval)
		{ UCVarValue val; val.Int = intval; SetGenericRep (val, CVAR_Int); return intval; }
	inline operator int () const { return Value; }
	inline int operator *() const { return Value; }

protected:
	virtual void DoSet (UCVarValue value, ECVarType type);

	int Value;
	int DefaultValue;

	friend class FFlagCVar;
};

class FFloatCVar : public FBaseCVar
{
public:
	FFloatCVar (const char *name, float def, DWORD flags, void (*callback)(FFloatCVar &)=NULL);

	virtual ECVarType GetRealType () const;

	virtual UCVarValue GetGenericRep (ECVarType type) const;
	virtual UCVarValue GetFavoriteRep (ECVarType *type) const;
	virtual UCVarValue GetGenericRepDefault (ECVarType type) const;
	virtual UCVarValue GetFavoriteRepDefault (ECVarType *type) const;
	virtual void SetGenericRepDefault (UCVarValue value, ECVarType type);

	float operator= (float floatval)
		{ UCVarValue val; val.Float = floatval; SetGenericRep (val, CVAR_Float); return floatval; }
	inline operator float () const { return Value; }
	inline float operator *() const { return Value; }

protected:
	virtual void DoSet (UCVarValue value, ECVarType type);

	float Value;
	float DefaultValue;
};

class FStringCVar : public FBaseCVar
{
public:
	FStringCVar (const char *name, const char *def, DWORD flags, void (*callback)(FStringCVar &)=NULL);

	virtual ECVarType GetRealType () const;

	virtual UCVarValue GetGenericRep (ECVarType type) const;
	virtual UCVarValue GetFavoriteRep (ECVarType *type) const;
	virtual UCVarValue GetGenericRepDefault (ECVarType type) const;
	virtual UCVarValue GetFavoriteRepDefault (ECVarType *type) const;
	virtual void SetGenericRepDefault (UCVarValue value, ECVarType type);

	char *operator= (char *stringrep)
		{ UCVarValue val; val.String = stringrep; SetGenericRep (val, CVAR_String); return stringrep; }
	inline operator const char * () const { return Value; }
	inline const char *operator *() const { return Value; }

protected:
	virtual void DoSet (UCVarValue value, ECVarType type);

	char *Value;
	char *DefaultValue;
};

class FColorCVar : public FIntCVar
{
public:
	FColorCVar (const char *name, int def, DWORD flags, void (*callback)(FColorCVar &)=NULL);

	virtual ECVarType GetRealType () const;

	virtual UCVarValue GetGenericRep (ECVarType type) const;
	virtual UCVarValue GetGenericRepDefault (ECVarType type) const;
	virtual void SetGenericRepDefault (UCVarValue value, ECVarType type);

	inline operator DWORD () const { return Value; }
	inline DWORD operator *() const { return Value; }
	inline int GetIndex () const { return Index; }

protected:
	virtual void DoSet (UCVarValue value, ECVarType type);
	
	static UCVarValue FromInt2 (int value, ECVarType type);
	static int ToInt2 (UCVarValue value, ECVarType type);

	int Index;
};

class FFlagCVar : public FBaseCVar
{
public:
	FFlagCVar (const char *name, FIntCVar &realvar, DWORD bitval);

	virtual ECVarType GetRealType () const;

	virtual UCVarValue GetGenericRep (ECVarType type) const;
	virtual UCVarValue GetFavoriteRep (ECVarType *type) const;
	virtual UCVarValue GetGenericRepDefault (ECVarType type) const;
	virtual UCVarValue GetFavoriteRepDefault (ECVarType *type) const;
	virtual void SetGenericRepDefault (UCVarValue value, ECVarType type);

	bool operator= (bool boolval)
		{ UCVarValue val; val.Bool = boolval; SetGenericRep (val, CVAR_Bool); return boolval; }
	inline operator int () const { return (ValueVar & BitVal); }
	inline int operator *() const { return (ValueVar & BitVal); }

protected:
	virtual void DoSet (UCVarValue value, ECVarType type);

	FIntCVar &ValueVar;
	DWORD BitVal;
};

extern int cvar_defflags;

FBaseCVar *cvar_set (const char *var_name, const char *value);
FBaseCVar *cvar_forceset (const char *var_name, const char *value);

inline FBaseCVar *cvar_set (const char *var_name, const byte *value) { return cvar_set (var_name, (const char *)value); }
inline FBaseCVar *cvar_forceset (const char *var_name, const byte *value) { return cvar_forceset (var_name, (const char *)value); }



// Maximum number of cvars that can be saved across a demo. If you need
// to save more, bump this up.
#define MAX_DEMOCVARS 32

// Restore demo cvars. Called after demo playback to restore all cvars
// that might possibly have been changed during the course of demo playback.
void C_RestoreCVars (void);


#define CUSTOM_CVAR(type,name,def,flags) \
	static void cvarfunc_##name(F##type##CVar &); \
	F##type##CVar name (#name, def, flags, cvarfunc_##name); \
	static void cvarfunc_##name(F##type##CVar &self)

#define CVAR(type,name,def,flags) \
	F##type##CVar name (#name, def, flags);

#define EXTERN_CVAR(type,name) extern F##type##CVar name;


#endif //__C_CVARS_H__