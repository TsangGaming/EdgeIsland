# Edge Island 正式签名指南

适用于 Windows 11 x64 的 MIT 项目 Edge Island。本文使用源码包内的两个 SignPath XML 配置和两个 GitHub Actions 工作流。测试包当前**未签名**。申请、公开仓库和发布版本均由项目所有者决定。

## 1　准备一个可申请的开源项目

SignPath Foundation 为获准的开源项目提供免费代码签名，私钥由服务保管。基金会会审核项目、维护情况和构建来源；采用 MIT License 只是其中一个条件。获批后，Windows 根据实际签名证书显示发布者，证书通常以基金会名义提供。独立 EXE 即使拥有可信签名，新版本仍可能收到 SmartScreen 声誉提示。 [免费计划](https://signpath.org/) · [基金会条件](https://signpath.org/terms) · [Microsoft 对 SmartScreen 的说明](https://learn.microsoft.com/windows/apps/package-and-deploy/smartscreen-reputation)

1. **先在私有环境验证。** 解压源码，保留隐藏目录 `.github`。在 Windows 按 README 构建测试版。使用 GitHub 时，可先创建 **Private** 仓库。启用 Actions 后，在 **Actions → Windows unsigned build** 运行工作流。通过后从 **Artifacts → EdgeIsland-x64-UNSIGNED** 下载实际安装包、独立卸载器和哈希清单。
2. **填写真实维护人员。** 编辑 `CODE_SIGNING_POLICY.md` 的 Authors、Reviewers、Approvers 表格。为实际成员启用 GitHub 与 SignPath 可用的多因素认证。若你独自维护，申请时说明事实，请基金会确认可接受的审批安排。
3. **在你决定公开后准备预览发布。** 基金会要求项目已按拟签名的形式发布、持续维护并描述用途。公开仓库内应有完整代码、MIT 许可证、功能说明、隐私政策与签名政策。在 Release 页提供带 `UNSIGNED` 标识的 EXE、下载说明及 SHA-256。保留与这个版本对应的提交、标签和成功构建链接。
4. **让政策易于找到。** 首页及下载页均使用 **Code signing policy** 链接到政策文件，同时链接隐私政策。获批之前保留“待批准”的真实状态。 [基金会对开源项目的要求](https://signpath.org/terms)

## 2　提交申请与配置 SignPath

1. 打开官方 **Apply** 页面填写当前表单。准备项目名、维护者的联系邮箱、仓库地址、MIT 许可证地址、下载／Release 页、签名政策、隐私政策、Actions 构建记录和项目功能简介。可以说明软件使用原生 Win32 接口，签名对象是桌面程序、卸载器和外层安装包。以实际表单字段及基金会后续邮件为准。 [官方申请入口](https://signpath.org/apply)
2. 收到获批通知后，按通知接受组织邀请并确认负责人的身份和 MFA。记下 **Organization ID、Project slug、Signing policy slug**，以及正式证书显示的发布者名称。项目显示名与 slug 可能不同。 [项目配置](https://docs.signpath.io/projects)
3. 在组织里启用预定义的 **GitHub.com Trusted Build System**，关联到 Edge Island 项目并配置仓库来源。审计日志功能使用官方 SignPath GitHub App，授权给目标仓库。确认所有签名上游作业都在 GitHub-hosted runner 上执行。让签名策略只接受预期仓库和已审查的发布标签／工作流。 [GitHub 集成与来源验证](https://docs.signpath.io/trusted-build-systems/github)
4. 在项目的 **Artifact Configurations** 创建两个配置。slug 分别为 **payload** 与 **installer**。将 `signpath/payload.xml` 和 `signpath/installer.xml` 的完整内容分别导入对应配置。两者的根容器均为 ZIP，因为工作流提交的是 GitHub Actions 工件。两个配置都声明必填的 `version` 参数，并限制具体文件名、产品名称和版本。 [配置语法](https://docs.signpath.io/artifact-configuration/syntax) · [版本与名称约束示例](https://docs.signpath.io/artifact-configuration/examples)
5. 在正式 Signing Policy 选基金会批准的生产证书及时间戳设置，启用人工审批。建立本项目的 **CI user** 并授予该策略的 **Submitter** 权限。将真实维护者指定为 **Approver**。在 CI 用户账户生成提交用 API token。token 应直接录入 GitHub Secret。 [CI 用户](https://docs.signpath.io/users/) · [构建系统配置](https://docs.signpath.io/build-system-integration)

## 3　设置 GitHub 的变量与秘密

在仓库的 **Settings → Environments** 建立 `signpath-release`。在仓库方案支持时配置 required reviewers 和部署标签规则。正式签名仍需要 SignPath 的人工批准。将以下四个配置值加入 **Settings → Secrets and variables → Actions → Variables**，或放入同名 Environment 的 Variables。

| Variable | 所填的值 |
| --- | --- |
| `SIGNPATH_ORGANIZATION_ID` | 获批组织的实际 ID |
| `SIGNPATH_PROJECT_SLUG` | Edge Island 项目的 slug |
| `SIGNPATH_POLICY_SLUG` | 正式发布签名策略的 slug |
| `SIGNPATH_EXPECTED_PUBLISHER` | 获批生产证书的精确发布者名称，即证书的 SimpleName。按 SignPath 提供的证书确认，包括空格和大小写。 |

把 `SIGNPATH_API_TOKEN` 存为 **signpath-release Environment secret**。只有请求 SignPath 的步骤引用它。不要把 token、证书密码或私钥写入源码、Issue、截图或 Release 说明。

最后，在 **仓库级 Actions Variables** 设置 `SIGNPATH_ENABLED=true`。该开关在作业进入 Environment 之前读取，所以必须使用仓库或允许该仓库访问的组织变量。未获批时保持未设置或设为 `false`。

**两种工作流的权限**　`build.yml` 在普通 push、PR 和手动运行时编译未签名测试版。生产用的 `sign.yml` 仅在手动触发、开关为 true、且所选 ref 为符合项目版本的标签时执行。两个工作流都没有创建公开 Release 的写入权限。 [SignPath GitHub Action 的输入参数](https://docs.signpath.io/trusted-build-systems/github)

## 4　运行正式构建并批准两个请求

先确认默认分支已包含 `.github/workflows/sign.yml`。把正式版本号写入 `src/version.h` 的 `EDGE_DISPLAY_VERSION`，以 **0.2.0** 为例。等待普通构建与安装测试通过，再为审核过的提交创建标签。以下命令由仓库所有者在决定发布此版本后执行。

```bash
git tag -a v0.2.0 -m "Edge Island 0.2.0"
git push origin v0.2.0
gh workflow run sign.yml --ref v0.2.0
```

| 顺序 | 运行中要核对的内容 |
| --- | --- |
| ① 构建 | GitHub 托管 Windows 运行器检出标签。工作流核对标签与版本头文件，以 0.2.0.0 作为 PE 的四段版本。 |
| ② 审批 payload | 在 SignPath 核对仓库、提交 SHA、标签、运行编号、产品名、版本及两个文件名 `EdgeIsland.exe` 和 `Uninstall.exe`。审批后，工作流验证两份签名、发布者与时间戳。 |
| ③ 封装 | 将已签的两个文件作为资源嵌入 `EdgeIslandSetup.exe`，并对比它们和上一步获批文件的完整字节。 |
| ④ 审批 installer | 在同一运行下核对第二个请求。确认它使用 installer 配置、同一个源代码提交、同一版本，再批准外层安装包签名。 |
| ⑤ 测试 | 工作流验证三份 EXE、内嵌内容以及安装与卸载流程。全部通过后提供命名含 `-signed` 的 Actions 工件。 |

两个请求各预留两小时等待审批。若超时，先查看现有请求和 GitHub 运行日志，再决定是否重新触发。源码、工作流或上游产物有变化时重新审查。 [受信任构建流程](https://docs.signpath.io/trusted-build-systems/github)

## 5　核验、发布与常见问题

从成功运行下载签名工件。在 Windows 上右键安装包 → **属性 → 数字签名 → 详细信息**，查看证书、签名有效性和时间戳。也可以在正式版下载目录运行以下命令。把示例版本替换为实际版本。

```powershell
$file = '.\EdgeIsland-0.2.0-x64-Setup.exe'
$sig = Get-AuthenticodeSignature -LiteralPath $file
$sig.Status
$sig.SignerCertificate.Subject
$sig.TimeStamperCertificate.Subject
Get-FileHash -Algorithm SHA256 -LiteralPath $file
```

预期 `Status` 为 `Valid`，发布者与批准策略一致，有时间戳。SHA-256 应与同一次构建的 `SHA256SUMS.txt` 相符。可以在安装目录对 `EdgeIsland.exe` 和 `Uninstall.exe` 再次执行验证。

**由维护者发布。** 在 GitHub 手动创建或编辑对应的 Release，附上该次构建的正式安装包、独立卸载器和 SHA-256 清单。发布说明写明版本、签名状态与已知限制，并链接 **Code signing policy** 和隐私政策。获批并确认投入使用后，在首页／签名政策按基金会要求加入：*Free code signing provided by SignPath.io, certificate by SignPath Foundation.* [署名及角色要求](https://signpath.org/terms)

| 遇到的情况 | 检查 |
| --- | --- |
| 签名作业显示 Skipped | `SIGNPATH_ENABLED` 是否为仓库级字符串 true，触发时是否选中了与版本一致的 v 标签。 |
| 返回 401／403 | CI token、组织 ID、两个 slug，以及该 CI 用户在对应策略上的 Submitter 权限。 |
| 工件或 XML 不匹配 | 确认上传的是 Actions 工件 ID、ZIP 根目录直接包含所列 EXE、四段版本正确且两个配置 slug 与文件名完全一致。 |
| 已签外层但内部验证失败 | 检查 setup 阶段的输入是否是第一批签回的文件。保留失败状态，重新封装正确的已签文件。 |
| 有签名但仍有 SmartScreen 提示 | 分别检查证书信任链、时间戳及 SmartScreen 声誉。新文件的声誉可能需要积累。 [Microsoft 官方说明](https://learn.microsoft.com/windows/apps/package-and-deploy/smartscreen-reputation) |

若基金会暂未接受申请，保留清晰标注的未签名测试渠道，依照反馈补充维护与发布记录。获批没有固定时限。
