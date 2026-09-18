package com.contrarynr.mesh.entity;
import jakarta.persistence.Entity;
import jakarta.persistence.GeneratedValue;
import jakarta.persistence.GenerationType;
import jakarta.persistence.Id;
import jakarta.persistence.Index;
import jakarta.persistence.Table;
import lombok.Getter;
import lombok.Setter;
@Entity
@Table(name = "peer_stat_record", indexes = {
        @Index(name = "idx_psr_ts", columnList = "ts"),
        @Index(name = "idx_psr_edge_ts", columnList = "sourceHostNum, peerHostNum, ts")
})
@Getter
@Setter
public class PeerStatRecord
{
    @Id
    @GeneratedValue(strategy = GenerationType.IDENTITY)
    private Long id;
    private int sourceHostNum;
    private int peerHostNum;
    private int channel;
    private Integer rtt;        //往返时延 ms(建连早期可能无值,允许为 null)
    private Long up;            //上行速率 B/s(客户端按真实采集周期对累计字节差分((周期尾流量-周期首流量)/周期长);null = traffic 字段组未配置)
    private Long down;          //下行速率 B/s(null 同上,与"空闲连接的 0 速率"区分)
    private Long buffered;      //发送缓冲积压 B(null = buffered 字段组未配置)
    private String pcState;     //PeerConnection 状态
    private String iceState;    //ICE 状态
    private String netPath;     //链路层级: lan局域网直连/wan公网直连/relay中继(打洞失败降级)
    private long ts;
    //JPA 要求无参构造
    public PeerStatRecord() {}
    //含全部成员的构造:摄入项落库时一次建好,免去一连串 setter();缺省字段用 null 直接带过
    public PeerStatRecord(int sourceHostNum, int peerHostNum, int channel, Integer rtt, Long up,
        Long down, Long buffered, String pcState, String iceState, String netPath, long ts)
    {this.sourceHostNum = sourceHostNum;this.peerHostNum = peerHostNum;this.channel = channel;
        this.rtt = rtt;this.up = up;this.down = down;this.buffered = buffered;
        this.pcState = pcState;this.iceState = iceState;this.netPath = netPath;this.ts = ts;}
}
