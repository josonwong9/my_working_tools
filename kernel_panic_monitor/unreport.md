产品线<kernel-monitor-pkg>补安装
====================

> **脱敏说明**：本文件中的内网 IP、主机名、产品线/命名空间、机房名称、负责人姓名、内部 RPM 包名与内部工具名均已替换为占位符（如 `<ip>`、`<hostname>`、`<namespace>`、`<region>`、`<owner>`、`<kernel-monitor-pkg>`、`BATCH_HOST_TOOL`、`BATCH_SSH_TOOL`），实际使用时请对照内部信息填写。

#### 查看产品线下未安装列表，以<namespace>为例
    所有机房
    $curl "http://<kernel-monitor-api>:8011/panic/api/get_unreported_list/?ns=<namespace>"
    
    机房A
    $curl "http://<kernel-monitor-api>:8011/panic/api/get_unreported_list/?ns=<namespace>&region=<region-list>"
    
    机房B + 机房C
    $curl "http://<kernel-monitor-api>:8011/panic/api/get_unreported_list/?ns=<namespace>&region=<region-list>"
    
##### 输出如下：
    <namespace>      <ip>   <hostname>  <region>
    ......省略.......
    <namespace>      <ip>   <hostname>        <region>
    # count: 105 《===一共有多少台

#### 批量安装(补缺)
    //批量添加BATCH_HOST_TOOL
    $BATCH_HOST_TOOL -a "`curl 'http://<kernel-monitor-api>:8011/panic/api/get_unreported_list/?ns=<namespace>&region=<region-list>' -s | grep -v '#' | awk '{ print $2}' |tr '\n' ' '`"
    
    //确认下列表
    $BATCH_HOST_TOOL 
    
    //批量安装
    $BATCH_SSH_TOOL -u root 'yum --enablerepo=\* clean all && yum -y install <kernel-monitor-pkg> --enablerepo=<internal-yum-repo>'
#### 未上节点汇总（自有机房）
| 节点 |未部署计数 | 运维负责人|
| ------------- |:-------------:| -----:|
|<namespace>|30|<owner>|
|<namespace>| 369 |比较杂，没有一个统一的人|
|<namespace>|16| <owner>|
|<namespace>|90 |<owner>|
|<namespace>|63 |<owner>|
|<namespace>|75| <owner>|
|<namespace>|36 |<owner>|
|<namespace>|24|<owner>|
|<namespace>|49|<owner>|
|<namespace>|44|<owner>|
|<namespace>|16| <owner>|
|<namespace>|31 |<owner>|
|<namespace>|9 |<owner>|
|<namespace>|12 |<owner>|
|<namespace>|20| <owner>|
|<namespace>|7| <owner>|
|<namespace>|19 |<owner>|
|<namespace>|15| <owner>|
|<namespace>|11 |<owner>|
|<namespace>|1 |<owner>|
|<namespace>|4 |<owner>|
|<namespace>|4 |<owner>|
|<namespace>|4 |<owner>|
|<namespace>|3| <owner>|
|<namespace>|3 |<owner>|
|<namespace>|2 |<owner>|
|<namespace>|2 |<owner>|
|<namespace>|2| 未知|
|<namespace>|2| <owner>|
|<namespace>|1| <owner>|
#### 未上节点汇总（含外部机房）
|节点  |  机器总量  | 未上报总量|
| ------------- |:-------------:| -----:|
summary	|34152 |-
<namespace>|	2855|	904
<namespace>	|3527|	464
<namespace>	|2595	|435
<namespace>|	5230|	389
<namespace>|	433|	380
<namespace>|	1128|	213
<namespace>|	499	|102
<namespace>|	514|	85
<namespace>	|440|	83
<namespace>|	4233	|70
<namespace>|	2994|	59
<namespace>|	370|	57
<namespace>|	542|	53
<namespace>|	50	|45
<namespace>	|441	|27
<namespace>	|29	|25
<namespace>	|299|	23
<namespace>	|124|	20
<namespace>|	116	|20
<namespace>|	293|	15
<namespace>|	128|	14
<namespace>	|155|	11
<namespace>|	23|	10
<namespace>|	2172|	9
<namespace>|	59|	9
<namespace>|	1845|	7
<namespace>|	22|	4
<namespace>	|56	|3
<namespace>|	31|	3
<namespace>|	6|	3
<namespace>|	2114|	0
<namespace>|	212	|0
<namespace>|	155|	0
<namespace>|	113	|0
<namespace>|	76|	0
<namespace>	|68|	0
<namespace>|	20|	0
<namespace>|	15|	0
<namespace>|	15|	0
<namespace>	|8|	0
<namespace>|	1	|0
<namespace>|	0|	0
<namespace>|	0	|0
<namespace>	|0|	0
<namespace>|	0  | 0

#### 详细列表=》总计: 966（包含机房B114、机房C147、机房A705）
| 节点        | IP           | 主机名  | 机房|
| ------------- |:-------------:| -----:|-----:|
|<namespace>|<ip>|<hostname>|<region>|
|<namespace>|<ip>|<hostname>|<region>|
|<namespace>|<ip>|<hostname>|<region>|

（完整明细共 966 台，此处仅保留 3 行作为格式示例，其余省略；完整列表可通过上文接口按需查询）
