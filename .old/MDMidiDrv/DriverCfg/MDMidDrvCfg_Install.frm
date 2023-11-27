VERSION 5.00
Begin VB.Form InstallForm 
   BorderStyle     =   1  'Fest Einfach
   Caption         =   "Install MIDI Driver"
   ClientHeight    =   3090
   ClientLeft      =   45
   ClientTop       =   435
   ClientWidth     =   4680
   KeyPreview      =   -1  'True
   LinkTopic       =   "Install MIDI Driver"
   MaxButton       =   0   'False
   ScaleHeight     =   206
   ScaleMode       =   3  'Pixel
   ScaleWidth      =   312
   StartUpPosition =   1  'Fenstermitte
   Begin VB.CheckBox BitCheck 
      Caption         =   "&64-Bit applications"
      Height          =   495
      Index           =   1
      Left            =   600
      TabIndex        =   2
      Top             =   1320
      Width           =   1935
   End
   Begin VB.CheckBox BitCheck 
      Caption         =   "&32-Bit applications"
      Height          =   495
      Index           =   0
      Left            =   600
      TabIndex        =   1
      Top             =   720
      Width           =   1815
   End
   Begin VB.CommandButton UninstButton 
      Caption         =   "&Uninstall Driver"
      Height          =   495
      Left            =   2640
      TabIndex        =   4
      Top             =   2280
      Width           =   1215
   End
   Begin VB.CommandButton InstallButton 
      Caption         =   "&Install Driver"
      Height          =   495
      Left            =   840
      TabIndex        =   3
      Top             =   2280
      Width           =   1215
   End
   Begin VB.Label BitLabel 
      Height          =   255
      Index           =   1
      Left            =   3120
      TabIndex        =   7
      Top             =   1455
      Width           =   1215
   End
   Begin VB.Label BitLabel 
      Height          =   255
      Index           =   0
      Left            =   3120
      TabIndex        =   6
      Top             =   855
      Width           =   1215
   End
   Begin VB.Label InstStateLabel 
      Caption         =   "State:"
      Height          =   255
      Left            =   2880
      TabIndex        =   5
      Top             =   360
      Width           =   975
   End
   Begin VB.Label InstallLabel 
      Caption         =   "Install for use with:"
      Height          =   255
      Left            =   240
      TabIndex        =   0
      Top             =   360
      Width           =   1455
   End
End
Attribute VB_Name = "InstallForm"
Attribute VB_GlobalNameSpace = False
Attribute VB_Creatable = False
Attribute VB_PredeclaredId = True
Attribute VB_Exposed = False
Option Explicit
'234567890123456789012345678901234567890123456789012345678901234567890123456789012345678
'000000001111111111222222222233333333334444444444555555555566666666667777777777888888888

Private Declare Function GetSystemDirectory Lib "kernel32" Alias _
    "GetSystemDirectoryA" (ByVal lpBuffer As String, ByVal uSize As Long) As Long

Private Const DRIVER_KEY = "SOFTWARE\Microsoft\Windows NT\CurrentVersion\Drivers32\"
Private Const DLL_NAME = "MDMidiDrv.dll"
Private Const DEF_WIN_DRV = "wdmaud.drv"

Private WOW_FLAGS As Long

Private Const INST_32BIT = &H0
Private Const INST_64BIT = &H1
Private InstState(&H0 To &H1) As Boolean

Private Sub BitCheck_Click(Index As Integer)

    Call RefreshButtons

End Sub

Private Sub Form_Load()

    'Wow64Mode = 1
    BitCheck(INST_32BIT).Enabled = True
    BitLabel(INST_32BIT).Enabled = True
    BitCheck(INST_32BIT).Value = vbChecked
    If Wow64Mode >= 1 Then
        BitCheck(INST_64BIT).Enabled = True
        BitLabel(INST_64BIT).Enabled = True
        BitCheck(INST_64BIT).Value = vbChecked
    Else
        BitCheck(INST_64BIT).Enabled = False
        BitLabel(INST_64BIT).Enabled = False
        BitCheck(INST_64BIT).Value = vbUnchecked
    End If
    
    Call RefreshInstState
    Call RefreshButtons

End Sub

Private Sub Form_KeyDown(KeyCode As Integer, Shift As Integer)

    If Shift = &H0 And KeyCode = vbKeyEscape Then
        Unload Me
    End If

End Sub

Private Sub RefreshInstState()

    WOW_FLAGS = KEY_WOW64_32KEY
    InstState(INST_32BIT) = (GetDriverSlot() > 0)
    
    If Wow64Mode > 0 Then
        WOW_FLAGS = KEY_WOW64_64KEY
        InstState(INST_64BIT) = (GetDriverSlot() > 0)
    Else
        InstState(INST_64BIT) = False
    End If
    
    BitLabel(INST_32BIT).Caption = IIf(InstState(INST_32BIT), "", "not ") & "installed"
    BitLabel(INST_64BIT).Caption = IIf(InstState(INST_64BIT), "", "not ") & "installed"

End Sub

Private Sub RefreshButtons()

    Dim InsEnable As Boolean
    Dim UnInsEnable As Boolean
    
    InsEnable = GetInsEnable(INST_32BIT, True) Or GetInsEnable(INST_64BIT, True)
    UnInsEnable = GetInsEnable(INST_32BIT, False) Or GetInsEnable(INST_64BIT, False)
    
    InstallButton.Enabled = InsEnable
    UninstButton.Enabled = UnInsEnable

End Sub

Private Function GetInsEnable(ByVal BitVal As Byte, ByVal Mode As Boolean) As Boolean

    ' Get state for the "Install" and "Uninstall" buttons.
    ' Mode = True: Install Button
    ' Mode = False: Uninstall Button
    
    If Wow64Mode <= 0 And BitVal = INST_64BIT Then
        GetInsEnable = False
    ElseIf BitCheck(BitVal).Value = vbChecked Then
        GetInsEnable = InstState(BitVal) Xor Mode
    Else
        GetInsEnable = False
    End If

End Function

Private Sub InstallButton_Click()

    Dim DLLPath As String
    Dim CurBit As Byte
    Dim RetVal As Byte
    Dim Results As Byte
    Dim ErrorStr As String
    
    DLLPath = GetDLLPath(DLL_NAME)
    If DLLPath = "" Then Exit Sub
    
    Results = &H0
    For CurBit = &H0 To &H1
        If BitCheck(CurBit).Value And Not InstState(CurBit) Then
            WOW_FLAGS = IIf(CurBit = &H1, KEY_WOW64_64KEY, KEY_WOW64_32KEY)
            ErrorStr = "Install error (x" & CStr(2 ^ (5 + CurBit)) & "):" & vbNewLine
            
            RetVal = InstallDriver(DLLPath)
            Select Case RetVal
            Case &H0
                ' no errors
            Case &H10
                MsgBox ErrorStr & "Unable to find free MIDI driver slot!", vbCritical
            Case &H20
                MsgBox ErrorStr & "Driver already installed!", vbInformation
            Case &H80
                MsgBox "Unable to change registry." & vbNewLine & _
                        "Please make sure you have administrator rights.", vbCritical
                Exit Sub
            Case Else
                MsgBox ErrorStr & "Unknown error 0x" & Hex$(RetVal) & "!", vbCritical
            End Select
            If RetVal Then
                Results = Results Or (2 ^ CurBit)
            End If
        End If
    Next CurBit
    
    Call RefreshInstState
    Call RefreshButtons
    If Results = &H0 Then
        MsgBox "Driver successfully installed.", vbInformation
    End If

End Sub

Private Sub UninstButton_Click()

    Dim DLLPath As String
    Dim CurBit As Byte
    Dim RetVal As Byte
    Dim Results As Byte
    Dim ErrorStr As String
    
    Results = &H0
    For CurBit = &H0 To &H1
        If BitCheck(CurBit).Value And InstState(CurBit) Then
            WOW_FLAGS = IIf(CurBit = &H1, KEY_WOW64_64KEY, KEY_WOW64_32KEY)
            ErrorStr = "Install error (x" & CStr(2 ^ (5 + CurBit)) & "):" & vbNewLine
            
            RetVal = UninstallDriver()
            Select Case RetVal
            Case &H0
                ' no errors
            Case &H10
                MsgBox ErrorStr & "Unable to find MIDI driver slot!" & vbNewLine & _
                        "The driver doesn't seem to be installed.", vbCritical
            Case &H80
                MsgBox "Unable to change registry." & vbNewLine & _
                        "Please make sure you have administrator rights.", vbCritical
                Exit Sub
            Case Else
                MsgBox ErrorStr & "Unknown error 0x" & Hex$(RetVal) & "!", vbCritical
            End Select
            If RetVal Then
                Results = Results Or (2 ^ CurBit)
            End If
        End If
    Next CurBit
    
    Call RefreshInstState
    Call RefreshButtons
    If Results = &H0 Then
        MsgBox "Driver successfully removed.", vbInformation
    End If

End Sub

Private Function GetDLLPath(ByVal DLLName As String) As String

    Dim SysDir As String
    Dim CurrDir As String
    Dim TempLng As Long
    Dim SearchDirs As String
    
    ' Get System directory ("C:\Windows\System32")
    SysDir = String$(MAX_PATH, &H0)
    TempLng = GetSystemDirectory(SysDir, Len(SysDir))
    SysDir = Left$(SysDir, TempLng)
    ' Append directory backslash if missing.
    If Right$(SysDir, 1) <> "\" Then SysDir = SysDir & "\"
    
    If Dir(SysDir & DLLName) <> "" Then
        GetDLLPath = DLLName    ' return DLL path relative to system directory
        Exit Function           ' (i.e. file title only)
    End If
    SearchDirs = SysDir
    
    ' Not found in system directory, try in current directory.
    CurrDir = CurDir()
    If Right$(CurrDir, 1) <> "\" Then CurrDir = CurrDir & "\"
    
    If StrComp(SysDir, CurrDir, vbTextCompare) <> 0 Then    ' yes, this MAY happen
        If Dir(CurrDir & DLLName) <> "" Then
            GetDLLPath = CurrDir & DLLName
            Exit Function
        End If
        SearchDirs = SearchDirs & vbNewLine & CurrDir
    End If
    
    MsgBox "Unable to find Driver DLL!" & vbNewLine & _
            "Searched directories:" & vbNewLine & vbNewLine & SearchDirs, vbCritical
    GetDLLPath = ""
    
End Function

Private Function InstallDriver(ByVal DLLName As String) As Byte

    Dim DrvSlot As Integer
    Dim KeyName As String
    Dim RetVal As Boolean
    
    DrvSlot = GetDriverSlot()
    If DrvSlot > 0 Then
        InstallDriver = &H20
        Exit Function
    End If
    
    DrvSlot = GetFreeDriverSlot()
    If DrvSlot = -1 Then
        InstallDriver = &H10
        Exit Function
    End If
    
    KeyName = "midi" & CStr(DrvSlot)
    
    RetVal = UpdateKey(HKEY_LOCAL_MACHINE, WOW_FLAGS, DRIVER_KEY, KeyName, DLLName)
    If Not RetVal Then
        InstallDriver = &H80
        Exit Function
    End If
    
    InstallDriver = &H0

End Function

Private Function UninstallDriver() As Byte

    Dim DrvSlot As Integer
    Dim KeyName As String
    Dim RetVal As Boolean
    Dim KeyVal As Variant
    
    DrvSlot = GetDriverSlot()
    If DrvSlot = 0 Then
        UninstallDriver = &H10
        Exit Function
    End If
    
    KeyVal = 0
    If DrvSlot < 9 Then
        KeyName = "midi" & CStr(DrvSlot + 1)
        KeyVal = GetKeyValue(HKEY_LOCAL_MACHINE, WOW_FLAGS, DRIVER_KEY, KeyName)
    End If
    KeyName = "midi" & CStr(DrvSlot)
    If IsNull(KeyVal) Then
        RetVal = DeleteKey(HKEY_LOCAL_MACHINE, WOW_FLAGS, DRIVER_KEY, KeyName)
    Else
        RetVal = UpdateKey(HKEY_LOCAL_MACHINE, WOW_FLAGS, DRIVER_KEY, KeyName, _
                            DEF_WIN_DRV)
    End If
    If Not RetVal Then
        UninstallDriver = &H80
        Exit Function
    End If
    
    UninstallDriver = &H0

End Function

Private Function GetDriverSlot() As Integer

    Dim DrvNum As Integer
    Dim KeyName As String
    Dim RetVal As Variant
    Dim DrvSlot As Integer
    Dim SlashPos As Long
    
    DrvSlot = 0
    For DrvNum = 1 To 9
        KeyName = "midi" & CStr(DrvNum)
        RetVal = GetKeyValue(HKEY_LOCAL_MACHINE, WOW_FLAGS, DRIVER_KEY, KeyName)
        If IsNull(RetVal) Then
            Exit For
        End If
        
        SlashPos = InStrRev(RetVal, "\")
        If SlashPos > 0 Then
            RetVal = Mid$(RetVal, SlashPos + 1)
        End If
        If StrComp(RetVal, "MDMidiDrv.dll", vbTextCompare) = 0 Then
            DrvSlot = DrvNum
            Exit For
        End If
    Next DrvNum
    
    GetDriverSlot = DrvSlot

End Function

Private Function GetFreeDriverSlot() As Integer

    Dim DrvNum As Integer
    Dim KeyName As String
    Dim RetVal As Variant
    Dim WMAudCount As Integer
    
    WMAudCount = 0
    For DrvNum = 0 To 9
        KeyName = "midi" & IIf(DrvNum, CStr(DrvNum), "")
        RetVal = GetKeyValue(HKEY_LOCAL_MACHINE, WOW_FLAGS, DRIVER_KEY, KeyName)
        If IsNull(RetVal) Then
            If DrvNum > 0 Then
                GetFreeDriverSlot = DrvNum
                Exit Function
            Else
                Exit For
            End If
        End If
        
        If StrComp(RetVal, DEF_WIN_DRV, vbTextCompare) = 0 Then
            WMAudCount = WMAudCount + 1
            If WMAudCount > 1 Then
                GetFreeDriverSlot = DrvNum
                Exit Function
            End If
        End If
    Next DrvNum
    
    GetFreeDriverSlot = -1

End Function
