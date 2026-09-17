package com.contrarynr.mesh.entity;
import jakarta.persistence.Entity;
import jakarta.persistence.GeneratedValue;
import jakarta.persistence.GenerationType;
import jakarta.persistence.Id;
import jakarta.persistence.Table;
import jakarta.persistence.UniqueConstraint;
import lombok.Getter;
import lombok.Setter;
@Entity
@Table(name = "peer_stat_aggregate", uniqueConstraints =
        @UniqueConstraint(name = "uk_psa_edge_window", columnNames = {"sourceHostNum", "peerHostNum", "channel", "windowStart"}))
@Getter
@Setter
public class PeerStatAggregate 
{
    @Id
    @GeneratedValue(strategy = GenerationType.IDENTITY)
    private Long id;
    private int sourceHostNum;  //上报方主机号
    private int peerHostNum;    //对端主机号
    private int channel;        //通道号: 0主通道/1文件传输/2音频/3视频
    private long windowStart;   //聚合窗口起点(毫秒纪元,对齐到整分钟)
    private double avgRtt;      //窗口内平均 RTT ms(样本全为 null 时记 0)
    private int sampleCount;    //窗口内样本数(速率是否可信,靠它判断)
}
