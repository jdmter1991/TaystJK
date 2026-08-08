sfx_sabers/saber_trail
{
	cull	twosided
    	sort additive        // <-- Add this line
    {
        map $whiteimage
        blendFunc GL_ONE GL_ONE
        rgbGen identity
    }
}

sfx_sabers/saber_trail_black
{
    cull twosided
    sort additive        // <-- Add this line
    {
        map $whiteimage
        blendFunc GL_SRC_ALPHA GL_ONE_MINUS_SRC_ALPHA
        rgbGen vertex
    }
}

sfx_sabers/saber_trail_ahsoka
{
    cull twosided
    sort additive
    {
        map $whiteimage
        blendFunc GL_SRC_ALPHA GL_ONE_MINUS_SRC_ALPHA
        glow
        rgbGen vertex
    }
    {
        map $whiteimage
        blendFunc GL_SRC_ALPHA GL_ONE_MINUS_SRC_ALPHA
        rgbGen identity
    }
}
sfx_sabers/saber_trail_black_ahsoka
{
    cull twosided
    sort additive
    {
        map $whiteimage
        blendFunc GL_SRC_ALPHA GL_ONE_MINUS_SRC_ALPHA
        rgbGen vertex
    }
}

sfx_sabers/saber_blade
{
    notc
    nopicmip
    cull twosided
    sort additive        // <-- Add this line

    {
        map sfx_sabers/saber_blade
        blendFunc GL_SRC_ALPHA GL_ONE
        rgbGen vertex
        alphaGen vertex
    }
}
sfx_sabers/saber_blade_ep3
{
	notc
	cull	twosided
    {
        map sfx_sabers/saber_blade_ep3
        blendFunc GL_ONE GL_ONE
        rgbGen vertex
    }
}


sfx_sabers/saber_end
{
	notc
	cull	twosided
    {
        map sfx_sabers/saber_end
        blendFunc GL_ONE GL_ONE
        rgbGen vertex
    }
}

sfx_sabers/saber_end_black
{
	notc
	cull	twosided
    {
        map sfx_sabers/saber_end_black
        blendFunc GL_SRC_ALPHA GL_ONE_MINUS_SRC_ALPHA
        rgbGen identity
	
    }
}