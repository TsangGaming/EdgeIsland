# Code signing policy

## 当前状态

Edge Island v0.3.0 提供明确标注为 `UNSIGNED` 的测试版本。本项目尚未获得 SignPath Foundation 批准。正式发布使用 Windows Authenticode 和受信任的时间戳。测试版本不会安装测试根证书，也不会更改 Defender、SmartScreen 或 Windows 执行策略。

## 负责人和授权

Edge Island 目前由独立开发者 TsangGaming 负责开发和维护，包括源代码、构建工作流、代码审核及签名审批。所有参与者必须在 GitHub 和 SignPath 使用多因素认证（MFA）。

| 角色 | 权限与责任 | 实际人员 |
| --- | --- | --- |
| Authors / Committers | 维护产品、构建脚本与代码签名配置 | [TsangGaming](https://github.com/TsangGaming) |
| Reviewers | 审核代码变更，特别关注依赖、安装器和工作流 | [TsangGaming](https://github.com/TsangGaming) |
| Approvers | 核对标签、提交、构建来源、版本及测试记录，在 SignPath 人工批准每次签名 | [TsangGaming](https://github.com/TsangGaming) |

目前本项目由单一维护者负责，暂时没有其他独立代码审核人员。

项目所有者应在申请时向基金会说明真实的团队结构。提交用 API token 仅获得提交权。生产审批和证书管理权由获准的维护者负责。

## 正式发布规则

1. 仅使用从项目审查过的源码标签，在获准的 GitHub 托管运行器构建的产物。只签 Edge Island 的主程序、卸载器和安装程序。
2. 签名请求使用两种受限文件配置。核对产品名称、产品版本、原始文件名与签名来源，先签内部程序，再封装并签外部安装包。
3. 审批者核对两个请求是否来自相同的仓库、提交、标签和工作流运行。每一批都应完成人工审批。
4. 发布前必须验证三份 EXE 的 Authenticode 信任状态、预期发布者、时间戳以及内嵌文件与已签名文件的逐字节一致性，并完成安装、卸载测试。
5. GitHub 工作流仅产生工件。由维护者决定何时公开仓库与发布版本。发布页标明签名状态、SHA-256，并链接到本政策和隐私政策。
6. 若发现构建来源、令牌或签名存在异常，暂停正式签名，冻结受影响的发布，撤销提交令牌，核查审计记录并联系 SignPath。通过官方仓库的安全报告入口联系维护者。发现冒用基金会签名的文件时可按基金会条款联系其团队。

## 获批后的署名

以下为获批、确认服务可用后才启用的公告文本。当前状态以本文件开头为准。

> Free code signing provided by SignPath.io, certificate by SignPath Foundation.

公开首页及下载／发布页使用 **Code signing policy** 作为标题或链接文字，并列出当前成员、签名状态和隐私政策。

[Privacy policy](PRIVACY.md) · [SignPath Foundation conditions](https://signpath.org/terms) · [SignPath Foundation](https://signpath.org/)
