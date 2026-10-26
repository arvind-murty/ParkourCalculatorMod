import net.minecraft.util.AxisAlignedBB;
public final class LadderGeometryProbe {
    public static void main(String[] args) {
        double half = (double) 0.6F / 2.0;
        double z = 2.01;
        AxisAlignedBB player = new AxisAlignedBB(0.5-half, 0.2, z-half, 0.5+half, 2.0, z+half);
        AxisAlignedBB ladder = new AxisAlignedBB(0, 0, 1, 1, 1, 1.125);
        for (double raw : new double[] {-0.2, -0.4, -0.8}) {
            double moved = ladder.calculateZOffset(player, raw);
            double endZ = z + moved;
            boolean before = Math.floor(z) == 1;
            boolean after = Math.floor(endZ) == 1;
            boolean hit = moved != raw;
            System.out.println("raw="+raw+" moved="+moved+" endZ="+endZ+" before="+before+" after="+after+" hit="+hit);
        }
    }
}
