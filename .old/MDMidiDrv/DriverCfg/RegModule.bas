Attribute VB_Name = "RegistryModule"
Option Explicit
'234567890123456789012345678901234567890123456789012345678901234567890123456789012345678
'000000001111111111222222222233333333334444444444555555555566666666667777777777888888888

' --- Functions to find out about running in 32-bit mode on a Win64 ---
Private Declare Function GetCurrentProcess Lib "kernel32" () As Long
Private Declare Function IsWow64Process Lib "kernel32" _
    (ByVal hProcess As Long, ByRef Wow64Process As Long) As Long


' --- Functions to access the registry ---
Private Declare Function RegCloseKey Lib "advapi32" (ByVal hKey As Long) As Long
Private Declare Function RegCreateKeyEx Lib "advapi32" Alias "RegCreateKeyExA" _
    (ByVal hKey As Long, ByVal lpSubKey As String, ByVal Reserved As Long, _
    ByVal lpClass As String, ByVal dwOptions As Long, ByVal samDesired As Long, _
    ByRef lpSecurityAttributes As Any, ByRef phkResult As Long, _
    ByRef lpdwDisposition As Long) As Long
Private Declare Function RegDeleteValue Lib "advapi32" Alias "RegDeleteValueA" _
    (ByVal hKey As Long, ByVal lpValueName As String) As Long
Private Declare Function RegOpenKeyEx Lib "advapi32" Alias "RegOpenKeyExA" _
    (ByVal hKey As Long, ByVal lpSubKey As String, ByVal ulOptions As Long, _
    ByVal samDesired As Long, ByRef phkResult As Long) As Long
Private Declare Function RegQueryValueEx Lib "advapi32" Alias "RegQueryValueExA" _
    (ByVal hKey As Long, ByVal lpValueName As String, ByVal lpReserved As Long, _
    ByRef lpType As Long, ByRef lpData As Any, ByRef lpcbData As Long) As Long
Private Declare Function RegSetValueEx Lib "advapi32" Alias "RegSetValueExA" _
    (ByVal hKey As Long, ByVal lpValueName As String, ByVal Reserved As Long, _
    ByVal dwType As Long, ByRef lpData As Any, ByVal cbData As Long) As Long

' Registry constants and types
Private Const REG_NONE = 0
Private Const REG_SZ = &H1
Private Const REG_EXPAND_SZ = &H2
Private Const REG_BINARY = &H3
Private Const REG_DWORD = &H4
Private Const REG_MULTI_SZ = &H7
Private Const REG_QWORD = &HB

Private Const REG_OPTION_NON_VOLATILE = &H0
Private Const REG_OPTION_VOLATILE = &H1

Private Const READ_CONTROL = &H20000
Private Const KEY_QUERY_VALUE = &H1
Private Const KEY_SET_VALUE = &H2
Private Const KEY_CREATE_SUB_KEY = &H4
Private Const KEY_ENUMERATE_SUB_KEYS = &H8
Private Const KEY_NOTIFY = &H10
Private Const KEY_CREATE_LINK = &H20
Private Const KEY_READ = KEY_QUERY_VALUE Or KEY_ENUMERATE_SUB_KEYS Or KEY_NOTIFY Or _
                        READ_CONTROL
Private Const KEY_WRITE = KEY_SET_VALUE Or KEY_CREATE_SUB_KEY Or READ_CONTROL
Private Const KEY_EXECUTE = KEY_READ
Private Const KEY_ALL_ACCESS = KEY_QUERY_VALUE Or KEY_SET_VALUE Or _
                                KEY_CREATE_SUB_KEY Or KEY_ENUMERATE_SUB_KEYS Or _
                                KEY_NOTIFY Or KEY_CREATE_LINK Or READ_CONTROL
Public Const KEY_WOW64_64KEY = &H100
Public Const KEY_WOW64_32KEY = &H200

Public Const HKEY_CLASSES_ROOT = &H80000000
Public Const HKEY_CURRENT_USER = &H80000001
Public Const HKEY_LOCAL_MACHINE = &H80000002
Public Const HKEY_USERS = &H80000003
Public Const HKEY_PERFORMANCE_DATA = &H80000004
Public Const HKEY_CURRENT_CONFIG = &H80000005
Public Const HKEY_DYN_DATA = &H80000006

Private Const ERROR_NONE = &H0
Private Const ERROR_BADKEY = &H2
Private Const ERROR_ACCESS_DENIED = &H8
Private Const ERROR_SUCCESS = &H0
Private Const ERROR_NO_MORE_ITEMS = &H103

Private Type SECURITY_ATTRIBUTES
    nLength As Long
    lpSecurityDescriptor As Long
    bInheritHandle As Boolean
End Type

Public Const MAX_PATH As Long = 260


Public Wow64Mode As Integer

Public Sub InitRegistryModule()

    Wow64Mode = IsWow64()

End Sub

Public Function IsWow64() As Integer

    Dim hProc As Long
    Dim Wow64Bool As Long
    Dim RetVal As Long
    
    hProc = GetCurrentProcess()
    
    ' IsWow64Process isn't available in older Windows versions.
    ' If that's the case, we're using a 32-bit Windows anyway.
    On Error GoTo NoWow64Error
    
    RetVal = IsWow64Process(hProc, Wow64Bool)
    If RetVal = &H0 Then
        ' An error occoured - assume Win32 with Wow64 APIs
        Wow64Bool = &H0
    End If
    
    IsWow64 = Wow64Bool
    
    Exit Function

NoWow64Error:

    IsWow64 = -1    ' 32-bit Windows without Wow64 APIs
    
    Exit Function

End Function


Public Function GetKeyValue(ByVal KeyRoot As Long, ByVal AddFlags As Long, _
                            ByVal KeyName As String, ByVal SubKeyRef As String) _
                            As Variant

    Dim RetVal As Long
    Dim hKey As Long
    'Dim hDepth As Long
    Dim KeyValType As Long
    Dim KeyValSize As Long
    Dim TempStr As String
    Dim TempLng As Long
    Dim ResVal As Variant
    
    ' If no Wow64 flags are supported or we're running Win32, mask the Wow64 bits out.
    If Wow64Mode <= 0 Then AddFlags = AddFlags And Not &HF00
    
    RetVal = RegOpenKeyEx(KeyRoot, KeyName, 0, AddFlags Or KEY_QUERY_VALUE, hKey)
    If RetVal <> ERROR_SUCCESS Then GoTo GetKeyError
    
    RetVal = RegQueryValueEx(hKey, SubKeyRef, &H0, KeyValType, ByVal &H0, KeyValSize)
    If RetVal <> ERROR_SUCCESS Then GoTo GetKeyError
    
    Select Case KeyValType
    Case REG_SZ, REG_EXPAND_SZ
        KeyValSize = MAX_PATH
        TempStr = String$(KeyValSize, &H0)
        RetVal = RegQueryValueEx(hKey, SubKeyRef, &H0, ByVal &H0, ByVal TempStr, _
                                KeyValSize)
        If RetVal <> ERROR_SUCCESS Then GoTo GetKeyError
        
        TempStr = Left$(TempStr, InStr(TempStr, Chr$(&H0)) - 1)
        ResVal = TempStr
    Case REG_DWORD
        KeyValSize = &H4
        RetVal = RegQueryValueEx(hKey, SubKeyRef, &H0, ByVal &H0, TempLng, KeyValSize)
        If RetVal <> ERROR_SUCCESS Then GoTo GetKeyError
        
        ResVal = TempLng
    Case Else
        ResVal = Null
    End Select
    
    RetVal = RegCloseKey(hKey)
    GetKeyValue = ResVal
    
    Exit Function
    
GetKeyError:

    If hKey <> &H0 Then
        RetVal = RegCloseKey(hKey)
    End If
    
    GetKeyValue = Null

End Function

Public Function UpdateKey(ByVal KeyRoot As Long, ByVal AddFlags As Long, _
                            ByVal KeyName As String, ByVal SubKeyName As String, _
                            ByVal SubKeyValue As Variant) As Boolean

    Dim RetVal As Long
    Dim hKey As Long
    Dim hDepth As Long
    Dim TempLng As Long
    Dim TempStr As String
    'Dim SecAttr As SECURITY_ATTRIBUTES
    
    'SecAttr.nLength = 50
    'SecAttr.lpSecurityDescriptor = &H0
    'SecAttr.bInheritHandle = True
    
    If Wow64Mode <= 0 Then AddFlags = AddFlags And Not &HF00
    
    RetVal = RegCreateKeyEx(KeyRoot, KeyName, &H0, REG_SZ, REG_OPTION_NON_VOLATILE, _
                        AddFlags Or KEY_SET_VALUE, ByVal 0, hKey, hDepth)
    If RetVal <> ERROR_SUCCESS Then GoTo CreateKeyError
    
    Select Case VarType(SubKeyValue)
    Case vbByte, vbInteger, vbLong
        TempLng = SubKeyValue
        RetVal = RegSetValueEx(hKey, SubKeyName, &H0, REG_DWORD, TempLng, &H4)
    Case vbString
        TempStr = SubKeyValue
        RetVal = RegSetValueEx(hKey, SubKeyName, &H0, REG_SZ, ByVal TempStr, _
                                Len(TempStr))
    Case Else
        GoTo CreateKeyError
    End Select
    If RetVal <> ERROR_SUCCESS Then GoTo CreateKeyError
    
    RetVal = RegCloseKey(hKey)
    UpdateKey = True
    
    Exit Function

CreateKeyError:

    If hKey <> &H0 Then
        RetVal = RegCloseKey(hKey)
    End If
    
    UpdateKey = False

End Function

Public Function DeleteKey(ByVal KeyRoot As Long, ByVal AddFlags As Long, _
                            ByVal KeyName As String, ByVal SubKeyName As String) _
                            As Boolean

    Dim RetVal As Long
    Dim hKey As Long
    Dim hDepth As Long
    
    If Wow64Mode <= 0 Then AddFlags = AddFlags And Not &HF00
    
    RetVal = RegCreateKeyEx(KeyRoot, KeyName, &H0, REG_SZ, REG_OPTION_NON_VOLATILE, _
                            AddFlags Or KEY_SET_VALUE, ByVal 0, hKey, hDepth)
    If RetVal <> ERROR_SUCCESS Then GoTo CreateKeyError
    
    RetVal = RegDeleteValue(hKey, SubKeyName)
    If RetVal <> ERROR_SUCCESS Then GoTo CreateKeyError
    
    RetVal = RegCloseKey(hKey)
    DeleteKey = True
    
    Exit Function

CreateKeyError:

    If hKey <> &H0 Then
        RetVal = RegCloseKey(hKey)
    End If
    
    DeleteKey = False

End Function
