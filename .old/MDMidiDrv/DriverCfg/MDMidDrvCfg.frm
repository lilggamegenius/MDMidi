VERSION 5.00
Begin VB.Form MainForm 
   BorderStyle     =   1  'Fest Einfach
   Caption         =   "mid2smps MIDI Driver Configuration"
   ClientHeight    =   3030
   ClientLeft      =   45
   ClientTop       =   375
   ClientWidth     =   6360
   LinkTopic       =   "Form1"
   MaxButton       =   0   'False
   ScaleHeight     =   202
   ScaleMode       =   3  'Pixel
   ScaleWidth      =   424
   StartUpPosition =   2  'Bildschirmmitte
   Begin VB.CommandButton SaveCfgButton 
      Caption         =   "&Save Settings"
      Height          =   495
      Left            =   4800
      TabIndex        =   18
      Top             =   2280
      Width           =   1215
   End
   Begin VB.CommandButton LoadCfgButton 
      Caption         =   "&Load Settings"
      Height          =   495
      Left            =   3360
      TabIndex        =   17
      Top             =   2280
      Width           =   1215
   End
   Begin VB.CommandButton ApplyButton 
      Caption         =   "&Apply Changes"
      Height          =   495
      Left            =   1800
      TabIndex        =   16
      Top             =   2280
      Width           =   1215
   End
   Begin VB.CommandButton InstallButton 
      Caption         =   "Un-/&Install ..."
      Height          =   495
      Left            =   240
      TabIndex        =   15
      Top             =   2280
      Width           =   1215
   End
   Begin VB.TextBox VolumeText 
      Alignment       =   2  'Zentriert
      BackColor       =   &H8000000F&
      BorderStyle     =   0  'Kein
      Height          =   285
      Left            =   5760
      Locked          =   -1  'True
      TabIndex        =   14
      Top             =   1680
      Width           =   375
   End
   Begin VB.HScrollBar VolumeScroll 
      Height          =   255
      LargeChange     =   100
      Left            =   1800
      Max             =   4000
      Min             =   100
      TabIndex        =   13
      Top             =   1680
      Value           =   100
      Width           =   3855
   End
   Begin VB.CommandButton DACFoldButton 
      Caption         =   ".."
      Height          =   255
      Left            =   5760
      TabIndex        =   11
      Top             =   1320
      Width           =   375
   End
   Begin VB.TextBox DACText 
      Height          =   285
      Left            =   1800
      TabIndex        =   10
      Top             =   1275
      Width           =   3855
   End
   Begin VB.CommandButton MapFoldButton 
      Caption         =   ".."
      Height          =   255
      Left            =   5760
      TabIndex        =   8
      Top             =   960
      Width           =   375
   End
   Begin VB.TextBox MapText 
      Height          =   285
      Left            =   1800
      TabIndex        =   7
      Top             =   915
      Width           =   3855
   End
   Begin VB.CommandButton PSGFoldButton 
      Caption         =   ".."
      Height          =   255
      Left            =   5760
      TabIndex        =   5
      Top             =   600
      Width           =   375
   End
   Begin VB.TextBox PSGText 
      Height          =   285
      Left            =   1800
      TabIndex        =   4
      Top             =   555
      Width           =   3855
   End
   Begin VB.CommandButton GYBFoldButton 
      Caption         =   ".."
      Height          =   255
      Left            =   5760
      TabIndex        =   2
      Top             =   240
      Width           =   375
   End
   Begin VB.TextBox GYBText 
      Height          =   285
      Left            =   1800
      TabIndex        =   1
      Top             =   195
      Width           =   3855
   End
   Begin VB.Label VolumeLabel 
      Caption         =   "&Volume:"
      Height          =   255
      Left            =   120
      TabIndex        =   12
      Top             =   1680
      Width           =   1575
   End
   Begin VB.Label DACLabel 
      Caption         =   "&DAC Data File:"
      Height          =   255
      Left            =   120
      TabIndex        =   9
      Top             =   1320
      Width           =   1575
   End
   Begin VB.Label MapLabel 
      Caption         =   "&Mappings File:"
      Height          =   255
      Left            =   120
      TabIndex        =   6
      Top             =   960
      Width           =   1575
   End
   Begin VB.Label PSGLabel 
      Caption         =   "&PSG Envelope File:"
      Height          =   255
      Left            =   120
      TabIndex        =   3
      Top             =   600
      Width           =   1575
   End
   Begin VB.Label GYBLabel 
      Caption         =   "&GYB Ins.-Lib. File:"
      Height          =   255
      Left            =   120
      TabIndex        =   0
      Top             =   240
      Width           =   1575
   End
End
Attribute VB_Name = "MainForm"
Attribute VB_GlobalNameSpace = False
Attribute VB_Creatable = False
Attribute VB_PredeclaredId = True
Attribute VB_Exposed = False
Option Explicit
'234567890123456789012345678901234567890123456789012345678901234567890123456789012345678
'000000001111111111222222222233333333334444444444555555555566666666667777777777888888888

Private Type OSVERSIONINFO
  dwOSVersionInfoSize As Long
  dwMajorVersion As Long
  dwMinorVersion As Long
  dwBuildNumber As Long
  dwPlatformId As Long
  szCSDVersion As String * &H80
End Type
Private Declare Function GetVersionEx Lib "kernel32" Alias _
    "GetVersionExA" (ByRef lpVersionInformation As OSVERSIONINFO) As Long

Private Const VER_PLATFORM_WIN32_WINDOWS = &H1
Private Const VER_PLATFORM_WIN32_NT = &H2


Private Const CFG_KEY = "SOFTWARE\MD MIDI Driver\"
Private Const KEY_GYB = "GYBPath"
Private Const KEY_PSG = "PSGPath"
Private Const KEY_MAP = "MappingPath"
Private Const KEY_DAC = "DACPath"
Private Const KEY_VOL = "Volume"

Private Sub Form_Load()

    Dim RetVal As Variant
    Dim OSVersion As OSVERSIONINFO
    
    OSVersion.dwOSVersionInfoSize = Len(OSVersion)
    Call GetVersionEx(OSVersion)
    If OSVersion.dwPlatformId <> VER_PLATFORM_WIN32_NT Then
        MsgBox "Sorry, but Windows 9x is not supported!", vbInformation
        End
    End If
    
    Call InitRegistryModule
    
    RetVal = GetKeyValue(HKEY_CURRENT_USER, &H0, CFG_KEY, KEY_GYB)
    GYBText.Text = IIf(IsNull(RetVal), "", RetVal)
    RetVal = GetKeyValue(HKEY_CURRENT_USER, &H0, CFG_KEY, KEY_PSG)
    PSGText.Text = IIf(IsNull(RetVal), "", RetVal)
    RetVal = GetKeyValue(HKEY_CURRENT_USER, &H0, CFG_KEY, KEY_MAP)
    MapText.Text = IIf(IsNull(RetVal), "", RetVal)
    RetVal = GetKeyValue(HKEY_CURRENT_USER, &H0, CFG_KEY, KEY_DAC)
    DACText.Text = IIf(IsNull(RetVal), "", RetVal)
    RetVal = GetKeyValue(HKEY_CURRENT_USER, &H0, CFG_KEY, KEY_VOL)
    VolumeScroll.Value = IIf(IsNull(RetVal), 1000, RetVal \ 10)

End Sub

Private Sub InstallButton_Click()

    InstallForm.Show vbModal

End Sub

Private Sub LoadCfgButton_Click()

    Dim FileName As String
    Dim CurLine As String
    
    FileName = OpenDialog("Configuration Files (*.cfg)|*.cfg")
    If FileName = "" Then Exit Sub
    
    On Error GoTo FileError
    
    Open FileName For Input As #1
        Line Input #1, CurLine
        GYBText.Text = CurLine
        Line Input #1, CurLine
        PSGText.Text = CurLine
        Line Input #1, CurLine
        MapText.Text = CurLine
        Line Input #1, CurLine
        DACText.Text = CurLine
        Line Input #1, CurLine
        VolumeScroll.Value = IIf(CurLine = "", 1000, CInt(CurLine))
    Close #1
    
    Exit Sub

FileError:

    Close #1
    
    MsgBox "Error while reading the file!", vbCritical
    
    Exit Sub

End Sub

Private Sub SaveCfgButton_Click()

    Dim FileName As String
    Dim CurLine As String
    
    FileName = SaveDialog("Configuration Files (*.cfg)|*.cfg")
    If FileName = "" Then Exit Sub
    
    On Error GoTo FileError
    
    Open FileName For Output As #2
        Print #2, GYBText.Text
        Print #2, PSGText.Text
        Print #2, MapText.Text
        Print #2, DACText.Text
        Print #2, CStr(VolumeScroll.Value)
    Close #2
    
    Exit Sub

FileError:

    Close #1
    
    MsgBox "Error while writing the file!", vbCritical
    
    Exit Sub

End Sub

Private Sub GYBFoldButton_Click()

    Dim FileName As String
    
    FileName = OpenDialog("GYB Instrument Files (*.gyb)|*.gyb", GYBText.Text)
    If FileName = "" Then Exit Sub
    
    GYBText.Text = FileName

End Sub

Private Sub PSGFoldButton_Click()

    Dim FileName As String
    
    FileName = OpenDialog("PSG Envelope Files (*.lst)|*.lst", PSGText.Text)
    If FileName = "" Then Exit Sub

    PSGText.Text = FileName

End Sub

Private Sub MapFoldButton_Click()

    Dim FileName As String
    
    FileName = OpenDialog("mid2smps Mapping Files (*.map)|*.map", MapText.Text)
    If FileName = "" Then Exit Sub

    MapText.Text = FileName

End Sub

Private Sub DACFoldButton_Click()

    Dim FileName As String
    
    FileName = OpenDialog("SMPSPlay DAC INI Files (*.ini)|*.ini", DACText.Text)
    If FileName = "" Then Exit Sub

    DACText.Text = FileName

End Sub

Private Sub VolumeScroll_Change()

    VolumeText.Text = CStr(VolumeScroll.Value)

End Sub

Private Sub ApplyButton_Click()

    Dim RetVal As Boolean
    
    RetVal = UpdateKey(HKEY_CURRENT_USER, &H0, CFG_KEY, KEY_GYB, GYBText.Text)
    If Not RetVal Then GoTo RegError
    RetVal = UpdateKey(HKEY_CURRENT_USER, &H0, CFG_KEY, KEY_PSG, PSGText.Text)
    If Not RetVal Then GoTo RegError
    RetVal = UpdateKey(HKEY_CURRENT_USER, &H0, CFG_KEY, KEY_MAP, MapText.Text)
    If Not RetVal Then GoTo RegError
    RetVal = UpdateKey(HKEY_CURRENT_USER, &H0, CFG_KEY, KEY_DAC, DACText.Text)
    If Not RetVal Then GoTo RegError
    RetVal = UpdateKey(HKEY_CURRENT_USER, &H0, CFG_KEY, KEY_VOL, _
                        CLng(VolumeScroll.Value * 10))
    If Not RetVal Then GoTo RegError
    MsgBox "Options written.", vbInformation
    
    Exit Sub

RegError:

    MsgBox "Unable to set registry keys!", vbCritical

    Exit Sub

End Sub



Private Function OpenDialog(ByVal Filter As String, _
                            Optional ByVal FileName As String) As String

    Dim OFLNM As OPENFILENAME
    Dim lReturn As Long
    
    Filter = Replace(Filter, "|", Chr$(0))
    With OFLNM
        .lStructSize = Len(OFLNM)
        .hwndOwner = Me.hWnd
        .hInstance = App.hInstance
        .lpstrFilter = Filter
        .nFilterIndex = 1
        .lpstrFile = IIf(IsMissing(FileName), "", FileName)
        .lpstrFile = .lpstrFile & String$(257 - Len(FileName), &H0)
        .nMaxFile = Len(.lpstrFile) - 1
        .lpstrFileTitle = .lpstrFile
        .nMaxFileTitle = .nMaxFile
        '.lpstrInitialDir = ""
        'If Title <> "" Then .lpstrTitle = Title
        .Flags = OFN_FILEMUSTEXIST Or OFN_HIDEREADONLY Or OFN_PATHMUSTEXIST
        .lpstrDefExt = Mid$(Filter, InStrRev(Filter, ".") + 1)
        
        lReturn = GetOpenFileName(OFLNM)
        If lReturn <> 0 Then
            OpenDialog = .lpstrFile
            OpenDialog = Left$(OpenDialog, InStr(1, OpenDialog, Chr$(0)) - 1)
        Else
            OpenDialog = ""
        End If
    
    End With

End Function

Private Function SaveDialog(Filter As String, Optional Title As String) As String
    
    Dim OFLNM As OPENFILENAME
    Dim lReturn As Long
    
    Dim intLen As Integer
    Dim strFileTitle As String
    
    Filter = Replace(Filter, "|", Chr$(&H0))
    With OFLNM
        .lStructSize = Len(OFLNM)
        .hwndOwner = Me.hWnd
        .hInstance = App.hInstance
        .lpstrFilter = Filter
        .nFilterIndex = 1
        .lpstrFile = String$(&H101, &H0)
        .nMaxFile = Len(.lpstrFile) - 1
        .lpstrFileTitle = .lpstrFile
        .nMaxFileTitle = .nMaxFile
        '.lpstrInitialDir = ""
        If Title <> "" Then .lpstrTitle = Title
        .Flags = OFN_HIDEREADONLY Or OFN_PATHMUSTEXIST Or OFN_CREATEPROMPT
        .lpstrDefExt = Mid$(Filter, InStrRev(Filter, Chr$(&H0)) + 3)
        
        lReturn = GetSaveFileName(OFLNM)
        If lReturn <> &H0 Then
            SaveDialog = .lpstrFile
            SaveDialog = Left$(SaveDialog, InStr(1, SaveDialog, Chr$(&H0)) - 1)
        Else
            SaveDialog = ""
        End If
    
    End With

End Function
