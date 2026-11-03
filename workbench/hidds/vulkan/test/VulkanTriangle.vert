#version 450
/* Three corners a quarter pixel off the 64x64 pixel grid: no pixel
   centre lies on an edge, so coverage is exact under any fill rule. */
const vec2 corners[3] = vec2[3](vec2(-0.7421875, -0.7421875),
                                vec2( 0.7578125, -0.7421875),
                                vec2(-0.7421875,  0.7578125));
void main()
{
    gl_Position = vec4(corners[gl_VertexIndex], 0.0, 1.0);
}
